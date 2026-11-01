// SPDX-License-Identifier: GPL-2.0
/*
 * ili9341_drm.c - DRM/KMS driver for ILI9341 SPI TFT display panels
 *
 * Drives a 240x320 ILI9341 panel over a 4-wire SPI bus (SCLK, MOSI, CS and
 * a separate D/C GPIO). The panel has its own frame memory (GRAM), so the
 * driver has no scan-out engine to program. Instead it:
 *
 *   - exposes one plane -> CRTC -> encoder -> connector pipeline to DRM,
 *   - keeps framebuffers in system memory (GEM shmem helpers),
 *   - converts the damaged part of the framebuffer to big-endian RGB565 and
 *     writes it into the panel's GRAM on every atomic commit.
 *
 * Features:
 *   - XRGB8888 and RGB565 framebuffers, damage clips (only changed
 *     rectangles are sent over SPI)
 *   - rotation 0/90/180/270 from the Device Tree "rotation" property
 *   - optional reset GPIO, power regulator and backlight device
 *   - fbdev emulation (/dev/fbN) for consoles and simple applications
 *   - system suspend/resume, shutdown, and safe unbind while user space
 *     still holds the device open
 *
 * Device Tree example:
 *
 *	display@0 {
 *		compatible = "training,ili9341-drm";
 *		reg = <0>;
 *		spi-max-frequency = <32000000>;
 *		dc-gpios = <&gpio 24 GPIO_ACTIVE_HIGH>;
 *		reset-gpios = <&gpio 25 GPIO_ACTIVE_LOW>;
 *		rotation = <90>;
 *		backlight = <&backlight>;
 *	};
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <linux/version.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_encoder.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_modes.h>
#include <drm/drm_modeset_helper.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_plane.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_rect.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 13, 0)
#include <drm/clients/drm_client_setup.h>
#include <drm/drm_fbdev_shmem.h>
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
#include <drm/drm_fbdev_shmem.h>
#else
#include <drm/drm_fbdev_generic.h>
#endif

#define DRIVER_NAME	"ili9341-train"
#define DRIVER_DESC	"ILI9341 SPI TFT DRM driver (training)"

/* Native panel geometry (portrait, rotation 0) */
#define ILI9341_WIDTH		240
#define ILI9341_HEIGHT		320
#define ILI9341_WIDTH_MM	37	/* 2.4" panel */
#define ILI9341_HEIGHT_MM	49

/* MIPI DCS commands */
#define ILI9341_SWRESET		0x01
#define ILI9341_SLPIN		0x10
#define ILI9341_SLPOUT		0x11
#define ILI9341_GAMSET		0x26
#define ILI9341_DISPOFF		0x28
#define ILI9341_DISPON		0x29
#define ILI9341_CASET		0x2a
#define ILI9341_PASET		0x2b
#define ILI9341_RAMWR		0x2c
#define ILI9341_MADCTL		0x36
#define ILI9341_PIXSET		0x3a

/* ILI9341 extended commands */
#define ILI9341_FRMCTR1		0xb1
#define ILI9341_DISCTRL		0xb6
#define ILI9341_ETMOD		0xb7
#define ILI9341_PWCTRL1		0xc0
#define ILI9341_PWCTRL2		0xc1
#define ILI9341_VMCTRL1		0xc5
#define ILI9341_VMCTRL2		0xc7
#define ILI9341_PWCTRLA		0xcb
#define ILI9341_PWCTRLB		0xcf
#define ILI9341_PGAMCTRL	0xe0
#define ILI9341_NGAMCTRL	0xe1
#define ILI9341_DTCA		0xe8
#define ILI9341_DTCB		0xea
#define ILI9341_PWRSEQ		0xed
#define ILI9341_EN3GAM		0xf2
#define ILI9341_PUMPCTRL	0xf7

/* MADCTL bits */
#define MADCTL_MY		BIT(7)	/* row address order */
#define MADCTL_MX		BIT(6)	/* column address order */
#define MADCTL_MV		BIT(5)	/* row/column exchange */
#define MADCTL_BGR		BIT(3)	/* BGR colour filter order */

#define ILI9341_PIXSET_16BPP	0x55

#define ILI9341_CMD_BUF_SIZE	32

struct ili9341 {
	struct drm_device drm;

	struct spi_device *spi;
	struct gpio_desc *dc;		/* low = command, high = data */
	struct gpio_desc *reset;	/* optional */
	struct regulator *supply;
	struct backlight_device *backlight;	/* optional */

	unsigned int rotation;
	bool bgr;
	bool powered;

	/* Serialises SPI command sequences (cmd byte + parameters/pixels) */
	struct mutex cmdlock;
	u8 *cmd_buf;		/* DMA-safe buffer for command bytes/params */
	u8 *tx_buf;		/* DMA-safe RGB565BE frame buffer */
	size_t tx_size;

	struct drm_display_mode mode;
	struct drm_plane plane;
	struct drm_crtc crtc;
	struct drm_encoder encoder;
	struct drm_connector connector;
};

static inline struct ili9341 *drm_to_ili(struct drm_device *drm)
{
	return container_of(drm, struct ili9341, drm);
}

/* ------------------------------------------------------------------------
 * SPI transport
 * ------------------------------------------------------------------------
 */

static int ili9341_spi_write(struct ili9341 *ili, int dc, const void *buf,
			     size_t len)
{
	size_t max_chunk = spi_max_transfer_size(ili->spi);
	int ret;

	/* Keep chunks a multiple of 2 so a pixel never straddles two chunks */
	max_chunk = min_t(size_t, max_chunk, SZ_64K) & ~1;
	if (!max_chunk)
		max_chunk = 2;

	gpiod_set_value_cansleep(ili->dc, dc);

	while (len) {
		struct spi_transfer tr = {
			.tx_buf = buf,
			.len = min(len, max_chunk),
			.bits_per_word = 8,
		};

		ret = spi_sync_transfer(ili->spi, &tr, 1);
		if (ret)
			return ret;

		buf += tr.len;
		len -= tr.len;
	}

	return 0;
}

/* Send a command byte followed by @num parameter bytes. */
static int ili9341_command_buf(struct ili9341 *ili, u8 cmd, const u8 *par,
			       size_t num)
{
	int ret;

	if (WARN_ON(num > ILI9341_CMD_BUF_SIZE))
		return -EINVAL;

	mutex_lock(&ili->cmdlock);

	ili->cmd_buf[0] = cmd;
	ret = ili9341_spi_write(ili, 0, ili->cmd_buf, 1);
	if (!ret && num) {
		memcpy(ili->cmd_buf, par, num);
		ret = ili9341_spi_write(ili, 1, ili->cmd_buf, num);
	}

	mutex_unlock(&ili->cmdlock);

	if (ret)
		drm_err_ratelimited(&ili->drm, "command 0x%02x failed: %d\n",
				    cmd, ret);
	return ret;
}

#define ili9341_command(ili, cmd, seq...) \
({ \
	static const u8 d[] = { seq }; \
	ili9341_command_buf(ili, cmd, d, ARRAY_SIZE(d)); \
})

/* Write @len bytes of RGB565BE pixels from tx_buf into the current window. */
static int ili9341_write_pixels(struct ili9341 *ili, size_t len)
{
	int ret;

	mutex_lock(&ili->cmdlock);

	ili->cmd_buf[0] = ILI9341_RAMWR;
	ret = ili9341_spi_write(ili, 0, ili->cmd_buf, 1);
	if (!ret)
		ret = ili9341_spi_write(ili, 1, ili->tx_buf, len);

	mutex_unlock(&ili->cmdlock);

	return ret;
}

/* ------------------------------------------------------------------------
 * Panel control
 * ------------------------------------------------------------------------
 */

static u8 ili9341_madctl(struct ili9341 *ili)
{
	u8 madctl;

	switch (ili->rotation) {
	default:
	case 0:
		madctl = MADCTL_MX;
		break;
	case 90:
		madctl = MADCTL_MV;
		break;
	case 180:
		madctl = MADCTL_MY;
		break;
	case 270:
		madctl = MADCTL_MY | MADCTL_MX | MADCTL_MV;
		break;
	}

	if (ili->bgr)
		madctl |= MADCTL_BGR;

	return madctl;
}

static void ili9341_hw_reset(struct ili9341 *ili)
{
	if (ili->reset) {
		gpiod_set_value_cansleep(ili->reset, 1);
		usleep_range(20, 1000);
		gpiod_set_value_cansleep(ili->reset, 0);
	} else {
		ili9341_command(ili, ILI9341_SWRESET);
	}
	/* The panel needs up to 120 ms after a reset before SLPOUT */
	msleep(120);
}

static int ili9341_power_on(struct ili9341 *ili)
{
	struct drm_device *drm = &ili->drm;
	u8 madctl;
	int ret;

	if (!ili->powered) {
		ret = regulator_enable(ili->supply);
		if (ret) {
			drm_err(drm, "failed to enable supply: %d\n", ret);
			return ret;
		}
		ili->powered = true;
	}

	ili9341_hw_reset(ili);

	ret = ili9341_command(ili, ILI9341_DISPOFF);
	if (ret)
		return ret;

	/* Power and timing setup (values from the panel vendor sequence) */
	ili9341_command(ili, ILI9341_PWCTRLB, 0x00, 0x83, 0x30);
	ili9341_command(ili, ILI9341_PWRSEQ, 0x64, 0x03, 0x12, 0x81);
	ili9341_command(ili, ILI9341_DTCA, 0x85, 0x01, 0x79);
	ili9341_command(ili, ILI9341_PWCTRLA, 0x39, 0x2c, 0x00, 0x34, 0x02);
	ili9341_command(ili, ILI9341_PUMPCTRL, 0x20);
	ili9341_command(ili, ILI9341_DTCB, 0x00, 0x00);

	/* Power control */
	ili9341_command(ili, ILI9341_PWCTRL1, 0x26);
	ili9341_command(ili, ILI9341_PWCTRL2, 0x11);
	/* VCOM */
	ili9341_command(ili, ILI9341_VMCTRL1, 0x35, 0x3e);
	ili9341_command(ili, ILI9341_VMCTRL2, 0xbe);

	/* 16 bits per pixel, frame rate 70 Hz */
	ili9341_command(ili, ILI9341_PIXSET, ILI9341_PIXSET_16BPP);
	ili9341_command(ili, ILI9341_FRMCTR1, 0x00, 0x1b);

	/* Gamma */
	ili9341_command(ili, ILI9341_EN3GAM, 0x08);
	ili9341_command(ili, ILI9341_GAMSET, 0x01);
	ili9341_command(ili, ILI9341_PGAMCTRL,
			0x1f, 0x1a, 0x18, 0x0a, 0x0f, 0x06, 0x45, 0x87,
			0x32, 0x0a, 0x07, 0x02, 0x07, 0x05, 0x00);
	ili9341_command(ili, ILI9341_NGAMCTRL,
			0x00, 0x25, 0x27, 0x05, 0x10, 0x09, 0x3a, 0x78,
			0x4d, 0x05, 0x18, 0x0d, 0x38, 0x3a, 0x1f);

	/* Entry mode, display function control */
	ili9341_command(ili, ILI9341_ETMOD, 0x07);
	ili9341_command(ili, ILI9341_DISCTRL, 0x0a, 0x82, 0x27, 0x00);

	ret = ili9341_command(ili, ILI9341_SLPOUT);
	if (ret)
		return ret;
	msleep(120);

	madctl = ili9341_madctl(ili);
	return ili9341_command_buf(ili, ILI9341_MADCTL, &madctl, 1);
}

static void ili9341_power_off(struct ili9341 *ili, bool hw_present)
{
	if (hw_present) {
		ili9341_command(ili, ILI9341_DISPOFF);
		ili9341_command(ili, ILI9341_SLPIN);
		usleep_range(5000, 6000);
	}

	if (ili->powered) {
		regulator_disable(ili->supply);
		ili->powered = false;
	}
}

/* ------------------------------------------------------------------------
 * Framebuffer -> panel transfer
 * ------------------------------------------------------------------------
 */

static void ili9341_convert(struct ili9341 *ili, const void *vaddr,
			    const struct drm_framebuffer *fb,
			    const struct drm_rect *clip)
{
	unsigned int cpp = fb->format->cpp[0];
	unsigned int w = drm_rect_width(clip);
	unsigned int h = drm_rect_height(clip);
	u8 *dst = ili->tx_buf;
	unsigned int x, y;

	vaddr += fb->offsets[0] + clip->y1 * fb->pitches[0] + clip->x1 * cpp;

	for (y = 0; y < h; y++, vaddr += fb->pitches[0]) {
		if (fb->format->format == DRM_FORMAT_XRGB8888) {
			const __le32 *src = vaddr;

			for (x = 0; x < w; x++) {
				u32 pix = le32_to_cpu(src[x]);
				u16 val = ((pix & 0x00f80000) >> 8) |
					  ((pix & 0x0000fc00) >> 5) |
					  ((pix & 0x000000f8) >> 3);

				*dst++ = val >> 8;
				*dst++ = val & 0xff;
			}
		} else { /* DRM_FORMAT_RGB565 */
			const __le16 *src = vaddr;

			for (x = 0; x < w; x++) {
				u16 val = le16_to_cpu(src[x]);

				*dst++ = val >> 8;
				*dst++ = val & 0xff;
			}
		}
	}
}

static int ili9341_set_window(struct ili9341 *ili, const struct drm_rect *r)
{
	u16 xs = r->x1, xe = r->x2 - 1, ys = r->y1, ye = r->y2 - 1;
	u8 par[4];
	int ret;

	par[0] = xs >> 8; par[1] = xs & 0xff;
	par[2] = xe >> 8; par[3] = xe & 0xff;
	ret = ili9341_command_buf(ili, ILI9341_CASET, par, 4);
	if (ret)
		return ret;

	par[0] = ys >> 8; par[1] = ys & 0xff;
	par[2] = ye >> 8; par[3] = ye & 0xff;
	return ili9341_command_buf(ili, ILI9341_PASET, par, 4);
}

static void ili9341_flush(struct ili9341 *ili, struct drm_framebuffer *fb,
			  const struct iosys_map *vmap, struct drm_rect *rect)
{
	struct drm_rect screen = DRM_RECT_INIT(0, 0, ili->mode.hdisplay,
					       ili->mode.vdisplay);
	size_t len;
	int ret;

	if (!drm_rect_intersect(rect, &screen))
		return;

	len = drm_rect_width(rect) * drm_rect_height(rect) * 2;
	if (WARN_ON(len > ili->tx_size))
		return;

	/* shmem buffers are always in system memory */
	if (WARN_ON_ONCE(vmap->is_iomem))
		return;

	ret = drm_gem_fb_begin_cpu_access(fb, DMA_FROM_DEVICE);
	if (ret)
		return;
	ili9341_convert(ili, vmap->vaddr, fb, rect);
	drm_gem_fb_end_cpu_access(fb, DMA_FROM_DEVICE);

	ret = ili9341_set_window(ili, rect);
	if (!ret)
		ret = ili9341_write_pixels(ili, len);
	if (ret)
		drm_err_once(&ili->drm, "failed to update display: %d\n", ret);
}

/* ------------------------------------------------------------------------
 * Plane
 * ------------------------------------------------------------------------
 */

static const u32 ili9341_formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_RGB565,
};

static const u64 ili9341_modifiers[] = {
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID,
};

static int ili9341_plane_atomic_check(struct drm_plane *plane,
				      struct drm_atomic_state *state)
{
	struct drm_plane_state *plane_state =
		drm_atomic_get_new_plane_state(state, plane);
	struct drm_crtc_state *crtc_state = NULL;

	if (plane_state->crtc)
		crtc_state = drm_atomic_get_new_crtc_state(state,
							   plane_state->crtc);

	/* No scaling and no positioning: the plane must cover the screen */
	return drm_atomic_helper_check_plane_state(plane_state, crtc_state,
						   DRM_PLANE_NO_SCALING,
						   DRM_PLANE_NO_SCALING,
						   false, false);
}

static void ili9341_plane_atomic_update(struct drm_plane *plane,
					struct drm_atomic_state *state)
{
	struct ili9341 *ili = drm_to_ili(plane->dev);
	struct drm_plane_state *plane_state =
		drm_atomic_get_new_plane_state(state, plane);
	struct drm_plane_state *old_state =
		drm_atomic_get_old_plane_state(state, plane);
	struct drm_shadow_plane_state *shadow =
		to_drm_shadow_plane_state(plane_state);
	struct drm_crtc_state *crtc_state;
	struct drm_rect rect;
	int idx;

	if (!plane_state->fb || !plane_state->crtc || !plane_state->visible)
		return;

	/*
	 * On a modeset the CRTC enable hook has just sent the full frame,
	 * so there is nothing more to do here.
	 */
	crtc_state = drm_atomic_get_new_crtc_state(state, plane_state->crtc);
	if (!crtc_state || !crtc_state->active ||
	    drm_atomic_crtc_needs_modeset(crtc_state))
		return;

	if (!drm_atomic_helper_damage_merged(old_state, plane_state, &rect))
		return;

	if (!drm_dev_enter(plane->dev, &idx))
		return;

	ili9341_flush(ili, plane_state->fb, &shadow->data[0], &rect);

	drm_dev_exit(idx);
}

static const struct drm_plane_helper_funcs ili9341_plane_helper_funcs = {
	DRM_GEM_SHADOW_PLANE_HELPER_FUNCS,
	.atomic_check = ili9341_plane_atomic_check,
	.atomic_update = ili9341_plane_atomic_update,
};

static const struct drm_plane_funcs ili9341_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = drm_plane_cleanup,
	DRM_GEM_SHADOW_PLANE_FUNCS,
};

/* ------------------------------------------------------------------------
 * CRTC
 * ------------------------------------------------------------------------
 */

static enum drm_mode_status
ili9341_crtc_mode_valid(struct drm_crtc *crtc,
			const struct drm_display_mode *mode)
{
	struct ili9341 *ili = drm_to_ili(crtc->dev);

	return drm_crtc_helper_mode_valid_fixed(crtc, mode, &ili->mode);
}

static int ili9341_crtc_atomic_check(struct drm_crtc *crtc,
				     struct drm_atomic_state *state)
{
	struct drm_crtc_state *crtc_state =
		drm_atomic_get_new_crtc_state(state, crtc);

	if (!crtc_state->enable)
		return 0;

	/* An enabled CRTC needs a framebuffer on the primary plane */
	return drm_atomic_helper_check_crtc_primary_plane(crtc_state);
}

static void ili9341_crtc_atomic_enable(struct drm_crtc *crtc,
				       struct drm_atomic_state *state)
{
	struct ili9341 *ili = drm_to_ili(crtc->dev);
	struct drm_plane_state *plane_state =
		drm_atomic_get_new_plane_state(state, crtc->primary);
	int idx;

	if (!drm_dev_enter(crtc->dev, &idx))
		return;

	if (ili9341_power_on(ili))
		goto out;

	/* Fill GRAM before the display is switched on to avoid garbage */
	if (plane_state && plane_state->fb && plane_state->visible) {
		struct drm_shadow_plane_state *shadow =
			to_drm_shadow_plane_state(plane_state);
		struct drm_rect full = DRM_RECT_INIT(0, 0,
				plane_state->fb->width, plane_state->fb->height);

		ili9341_flush(ili, plane_state->fb, &shadow->data[0], &full);
	}

	ili9341_command(ili, ILI9341_DISPON);
	msleep(20);
	backlight_enable(ili->backlight);

out:
	drm_dev_exit(idx);
}

static void ili9341_crtc_atomic_disable(struct drm_crtc *crtc,
					struct drm_atomic_state *state)
{
	struct ili9341 *ili = drm_to_ili(crtc->dev);
	bool present;
	int idx;

	backlight_disable(ili->backlight);

	/*
	 * After unbind the SPI device may be gone: skip the panel commands
	 * but still drop the regulator reference.
	 */
	present = drm_dev_enter(crtc->dev, &idx);
	ili9341_power_off(ili, present);
	if (present)
		drm_dev_exit(idx);
}

static const struct drm_crtc_helper_funcs ili9341_crtc_helper_funcs = {
	.mode_valid = ili9341_crtc_mode_valid,
	.atomic_check = ili9341_crtc_atomic_check,
	.atomic_enable = ili9341_crtc_atomic_enable,
	.atomic_disable = ili9341_crtc_atomic_disable,
};

static const struct drm_crtc_funcs ili9341_crtc_funcs = {
	.reset = drm_atomic_helper_crtc_reset,
	.destroy = drm_crtc_cleanup,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
};

/* ------------------------------------------------------------------------
 * Encoder and connector
 * ------------------------------------------------------------------------
 */

static const struct drm_encoder_funcs ili9341_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static int ili9341_connector_get_modes(struct drm_connector *connector)
{
	struct ili9341 *ili = drm_to_ili(connector->dev);

	return drm_connector_helper_get_modes_fixed(connector, &ili->mode);
}

static const struct drm_connector_helper_funcs ili9341_connector_helper_funcs = {
	.get_modes = ili9341_connector_get_modes,
};

static const struct drm_connector_funcs ili9341_connector_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

/* ------------------------------------------------------------------------
 * DRM device
 * ------------------------------------------------------------------------
 */

static const struct drm_mode_config_funcs ili9341_mode_config_funcs = {
	.fb_create = drm_gem_fb_create_with_dirty,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

DEFINE_DRM_GEM_FOPS(ili9341_fops);

static const struct drm_driver ili9341_drm_driver = {
	.driver_features = DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.fops = &ili9341_fops,
	DRM_GEM_SHMEM_DRIVER_OPS,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 13, 0)
	DRM_FBDEV_SHMEM_DRIVER_OPS,
#endif
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 14, 0)
	.date = "20261101",
#endif
	.major = 1,
	.minor = 0,
};

static int ili9341_modeset_init(struct ili9341 *ili)
{
	struct drm_device *drm = &ili->drm;
	struct drm_display_mode mode = {
		DRM_SIMPLE_MODE(ILI9341_WIDTH, ILI9341_HEIGHT,
				ILI9341_WIDTH_MM, ILI9341_HEIGHT_MM),
	};
	int ret;

	/* Landscape rotations swap the visible width and height */
	if (ili->rotation == 90 || ili->rotation == 270) {
		swap(mode.hdisplay, mode.vdisplay);
		swap(mode.hsync_start, mode.vsync_start);
		swap(mode.hsync_end, mode.vsync_end);
		swap(mode.htotal, mode.vtotal);
		swap(mode.width_mm, mode.height_mm);
	}
	drm_mode_copy(&ili->mode, &mode);

	ret = drmm_mode_config_init(drm);
	if (ret)
		return ret;

	drm->mode_config.min_width = ili->mode.hdisplay;
	drm->mode_config.max_width = ili->mode.hdisplay;
	drm->mode_config.min_height = ili->mode.vdisplay;
	drm->mode_config.max_height = ili->mode.vdisplay;
	drm->mode_config.preferred_depth = 24;
	drm->mode_config.funcs = &ili9341_mode_config_funcs;

	ret = drm_universal_plane_init(drm, &ili->plane, 0,
				       &ili9341_plane_funcs,
				       ili9341_formats,
				       ARRAY_SIZE(ili9341_formats),
				       ili9341_modifiers,
				       DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret)
		return ret;
	drm_plane_helper_add(&ili->plane, &ili9341_plane_helper_funcs);
	drm_plane_enable_fb_damage_clips(&ili->plane);

	ret = drm_crtc_init_with_planes(drm, &ili->crtc, &ili->plane, NULL,
					&ili9341_crtc_funcs, NULL);
	if (ret)
		return ret;
	drm_crtc_helper_add(&ili->crtc, &ili9341_crtc_helper_funcs);

	ret = drm_encoder_init(drm, &ili->encoder, &ili9341_encoder_funcs,
			       DRM_MODE_ENCODER_NONE, NULL);
	if (ret)
		return ret;
	ili->encoder.possible_crtcs = drm_crtc_mask(&ili->crtc);

	ret = drm_connector_init(drm, &ili->connector,
				 &ili9341_connector_funcs,
				 DRM_MODE_CONNECTOR_SPI);
	if (ret)
		return ret;
	drm_connector_helper_add(&ili->connector,
				 &ili9341_connector_helper_funcs);

	ret = drm_connector_attach_encoder(&ili->connector, &ili->encoder);
	if (ret)
		return ret;

	drm_mode_config_reset(drm);
	return 0;
}

/* ------------------------------------------------------------------------
 * SPI driver
 * ------------------------------------------------------------------------
 */

static int ili9341_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct drm_device *drm;
	struct ili9341 *ili;
	int ret;

	ili = devm_drm_dev_alloc(dev, &ili9341_drm_driver, struct ili9341, drm);
	if (IS_ERR(ili))
		return PTR_ERR(ili);
	drm = &ili->drm;
	ili->spi = spi;

	ret = drmm_mutex_init(drm, &ili->cmdlock);
	if (ret)
		return ret;

	ili->dc = devm_gpiod_get(dev, "dc", GPIOD_OUT_LOW);
	if (IS_ERR(ili->dc))
		return dev_err_probe(dev, PTR_ERR(ili->dc),
				     "failed to get dc gpio\n");

	/* Hold the panel in reset until the CRTC is enabled */
	ili->reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ili->reset))
		return dev_err_probe(dev, PTR_ERR(ili->reset),
				     "failed to get reset gpio\n");

	/* A dummy regulator is returned when no "power-supply" is given */
	ili->supply = devm_regulator_get(dev, "power");
	if (IS_ERR(ili->supply))
		return dev_err_probe(dev, PTR_ERR(ili->supply),
				     "failed to get power supply\n");

	ili->backlight = devm_of_find_backlight(dev);
	if (IS_ERR(ili->backlight))
		return dev_err_probe(dev, PTR_ERR(ili->backlight),
				     "failed to get backlight\n");

	ili->rotation = 0;
	device_property_read_u32(dev, "rotation", &ili->rotation);
	if (ili->rotation % 90 || ili->rotation > 270) {
		dev_err(dev, "invalid rotation %u (use 0, 90, 180 or 270)\n",
			ili->rotation);
		return -EINVAL;
	}
	/* Most ILI9341 modules have a BGR colour filter */
	ili->bgr = !device_property_read_bool(dev, "training,rgb-panel");

	spi->bits_per_word = 8;
	if (!spi->max_speed_hz)
		spi->max_speed_hz = 10000000;
	ret = spi_setup(spi);
	if (ret)
		return dev_err_probe(dev, ret, "spi_setup failed\n");

	ili->cmd_buf = devm_kmalloc(dev, ILI9341_CMD_BUF_SIZE, GFP_KERNEL);
	ili->tx_size = ILI9341_WIDTH * ILI9341_HEIGHT * 2;
	ili->tx_buf = devm_kmalloc(dev, ili->tx_size, GFP_KERNEL);
	if (!ili->cmd_buf || !ili->tx_buf)
		return -ENOMEM;

	ret = ili9341_modeset_init(ili);
	if (ret)
		return ret;

	spi_set_drvdata(spi, drm);

	ret = drm_dev_register(drm, 0);
	if (ret)
		return ret;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 13, 0)
	drm_client_setup(drm, NULL);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
	drm_fbdev_shmem_setup(drm, 0);
#else
	drm_fbdev_generic_setup(drm, 0);
#endif

	drm_info(drm, "%ux%u ILI9341 on %s, rotation %u, %u Hz SPI\n",
		 ili->mode.hdisplay, ili->mode.vdisplay, dev_name(dev),
		 ili->rotation, spi->max_speed_hz);
	return 0;
}

static void ili9341_remove(struct spi_device *spi)
{
	struct drm_device *drm = spi_get_drvdata(spi);

	/* Block new hardware access, then turn the panel off */
	drm_dev_unplug(drm);
	drm_atomic_helper_shutdown(drm);
}

static void ili9341_shutdown(struct spi_device *spi)
{
	drm_atomic_helper_shutdown(spi_get_drvdata(spi));
}

static int ili9341_pm_suspend(struct device *dev)
{
	return drm_mode_config_helper_suspend(dev_get_drvdata(dev));
}

static int ili9341_pm_resume(struct device *dev)
{
	return drm_mode_config_helper_resume(dev_get_drvdata(dev));
}

static DEFINE_SIMPLE_DEV_PM_OPS(ili9341_pm_ops, ili9341_pm_suspend,
				ili9341_pm_resume);

static const struct of_device_id ili9341_of_match[] = {
	{ .compatible = "training,ili9341-drm" },
	{ }
};
MODULE_DEVICE_TABLE(of, ili9341_of_match);

static const struct spi_device_id ili9341_spi_ids[] = {
	{ "ili9341-drm" },
	{ }
};
MODULE_DEVICE_TABLE(spi, ili9341_spi_ids);

static struct spi_driver ili9341_spi_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = ili9341_of_match,
		.pm = pm_sleep_ptr(&ili9341_pm_ops),
	},
	.id_table = ili9341_spi_ids,
	.probe = ili9341_probe,
	.remove = ili9341_remove,
	.shutdown = ili9341_shutdown,
};
module_spi_driver(ili9341_spi_driver);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Durairaj Gobi");
MODULE_LICENSE("GPL");
