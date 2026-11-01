// SPDX-License-Identifier: GPL-2.0
/*
 * disp_test.c - draw test patterns on the ILI9341 DRM display
 *
 * Uses only the kernel's DRM UAPI (no libdrm): finds the ili9341-train
 * card, creates a dumb XRGB8888 buffer, sets the mode, draws a pattern and
 * tells the driver which rectangles changed with DRM_IOCTL_MODE_DIRTYFB.
 *
 * Usage:
 *   disp_test [-d /dev/dri/cardN] info
 *   disp_test [-d /dev/dri/cardN] bars|gradient|grid [seconds]
 *   disp_test [-d /dev/dri/cardN] fill RRGGBB [seconds]
 *   disp_test [-d /dev/dri/cardN] bounce [seconds]
 *
 * Run it as root on a console with no other DRM master (no X/Wayland).
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <drm/drm.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_mode.h>

#define DRIVER_NAME "ili9341-train"

struct display {
	int fd;
	uint32_t conn_id, crtc_id, fb_id, handle;
	struct drm_mode_modeinfo mode;
	uint32_t width, height, pitch;
	uint64_t size;
	uint32_t *pix;
};

static void die(const char *what)
{
	perror(what);
	exit(1);
}

static int xioctl(int fd, unsigned long req, void *arg)
{
	int ret;

	do {
		ret = ioctl(fd, req, arg);
	} while (ret == -1 && (errno == EINTR || errno == EAGAIN));
	return ret;
}

static void *xcalloc(size_t n, size_t size)
{
	void *p = calloc(n ? n : 1, size);

	if (!p)
		die("calloc");
	return p;
}

static int is_our_card(int fd)
{
	struct drm_version v = { 0 };
	char name[64] = "";

	v.name = name;
	v.name_len = sizeof(name) - 1;
	if (xioctl(fd, DRM_IOCTL_VERSION, &v))
		return 0;
	return strcmp(name, DRIVER_NAME) == 0;
}

static int open_card(const char *path)
{
	char buf[32];
	int fd, i;

	if (path) {
		fd = open(path, O_RDWR | O_CLOEXEC);
		if (fd < 0)
			die(path);
		return fd;
	}

	for (i = 0; i < 16; i++) {
		snprintf(buf, sizeof(buf), "/dev/dri/card%d", i);
		fd = open(buf, O_RDWR | O_CLOEXEC);
		if (fd < 0)
			continue;
		if (is_our_card(fd)) {
			printf("using %s\n", buf);
			return fd;
		}
		close(fd);
	}

	fprintf(stderr, "no %s DRM device found (is the module loaded?)\n",
		DRIVER_NAME);
	exit(1);
}

/* Find a connected connector, its first mode and a CRTC that can drive it. */
static void find_pipeline(struct display *d)
{
	struct drm_mode_card_res res = { 0 };
	uint32_t *conns, *crtcs, *encs;
	uint32_t i;

	if (xioctl(d->fd, DRM_IOCTL_MODE_GETRESOURCES, &res))
		die("GETRESOURCES");

	conns = xcalloc(res.count_connectors, sizeof(*conns));
	crtcs = xcalloc(res.count_crtcs, sizeof(*crtcs));
	encs = xcalloc(res.count_encoders, sizeof(*encs));
	res.connector_id_ptr = (uintptr_t)conns;
	res.crtc_id_ptr = (uintptr_t)crtcs;
	res.encoder_id_ptr = (uintptr_t)encs;
	res.count_fbs = 0;
	if (xioctl(d->fd, DRM_IOCTL_MODE_GETRESOURCES, &res))
		die("GETRESOURCES");

	for (i = 0; i < res.count_connectors; i++) {
		struct drm_mode_get_connector c = { .connector_id = conns[i] };
		struct drm_mode_modeinfo *modes;
		uint32_t *c_encs;
		struct drm_mode_get_encoder e = { 0 };
		uint32_t j;

		if (xioctl(d->fd, DRM_IOCTL_MODE_GETCONNECTOR, &c))
			die("GETCONNECTOR");
		if (c.connection != 1 || !c.count_modes || !c.count_encoders)
			continue;

		modes = xcalloc(c.count_modes, sizeof(*modes));
		c_encs = xcalloc(c.count_encoders, sizeof(*c_encs));
		c.modes_ptr = (uintptr_t)modes;
		c.encoders_ptr = (uintptr_t)c_encs;
		c.count_props = 0;
		if (xioctl(d->fd, DRM_IOCTL_MODE_GETCONNECTOR, &c))
			die("GETCONNECTOR");

		e.encoder_id = c.encoder_id ? c.encoder_id : c_encs[0];
		if (xioctl(d->fd, DRM_IOCTL_MODE_GETENCODER, &e))
			die("GETENCODER");

		d->crtc_id = e.crtc_id;
		for (j = 0; !d->crtc_id && j < res.count_crtcs; j++)
			if (e.possible_crtcs & (1u << j))
				d->crtc_id = crtcs[j];

		d->conn_id = c.connector_id;
		d->mode = modes[0];
		d->width = modes[0].hdisplay;
		d->height = modes[0].vdisplay;
		free(modes);
		free(c_encs);
		break;
	}

	free(conns);
	free(crtcs);
	free(encs);

	if (!d->conn_id || !d->crtc_id) {
		fprintf(stderr, "no connected connector/CRTC found\n");
		exit(1);
	}
}

static void create_fb(struct display *d)
{
	struct drm_mode_create_dumb creq = {
		.width = d->width, .height = d->height, .bpp = 32,
	};
	struct drm_mode_fb_cmd2 fb = { 0 };
	struct drm_mode_map_dumb mreq = { 0 };

	if (xioctl(d->fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq))
		die("CREATE_DUMB");
	d->handle = creq.handle;
	d->pitch = creq.pitch;
	d->size = creq.size;

	fb.width = d->width;
	fb.height = d->height;
	fb.pixel_format = DRM_FORMAT_XRGB8888;
	fb.handles[0] = d->handle;
	fb.pitches[0] = d->pitch;
	if (xioctl(d->fd, DRM_IOCTL_MODE_ADDFB2, &fb))
		die("ADDFB2");
	d->fb_id = fb.fb_id;

	mreq.handle = d->handle;
	if (xioctl(d->fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq))
		die("MAP_DUMB");
	d->pix = mmap(NULL, d->size, PROT_READ | PROT_WRITE, MAP_SHARED,
		      d->fd, mreq.offset);
	if (d->pix == MAP_FAILED)
		die("mmap");
	memset(d->pix, 0, d->size);
}

static void set_crtc(struct display *d)
{
	struct drm_mode_crtc crtc = {
		.crtc_id = d->crtc_id,
		.fb_id = d->fb_id,
		.set_connectors_ptr = (uintptr_t)&d->conn_id,
		.count_connectors = 1,
		.mode = d->mode,
		.mode_valid = 1,
	};

	if (xioctl(d->fd, DRM_IOCTL_MODE_SETCRTC, &crtc))
		die("SETCRTC (is another program DRM master?)");
}

/* Tell the driver which rectangle changed; NULL means the whole screen. */
static void dirty(struct display *d, const struct drm_clip_rect *clip)
{
	struct drm_clip_rect full = { 0, 0, d->width, d->height };
	struct drm_mode_fb_dirty_cmd cmd = {
		.fb_id = d->fb_id,
		.num_clips = 1,
		.clips_ptr = (uintptr_t)(clip ? clip : &full),
	};

	if (xioctl(d->fd, DRM_IOCTL_MODE_DIRTYFB, &cmd) && errno != ENOSYS)
		perror("DIRTYFB");
}

static inline void put(struct display *d, uint32_t x, uint32_t y, uint32_t c)
{
	d->pix[y * (d->pitch / 4) + x] = c;
}

static void fill_rect(struct display *d, int x0, int y0, int w, int h,
		      uint32_t c)
{
	int x, y;

	for (y = y0; y < y0 + h; y++)
		for (x = x0; x < x0 + w; x++)
			if (x >= 0 && y >= 0 &&
			    (uint32_t)x < d->width && (uint32_t)y < d->height)
				put(d, x, y, c);
}

static void draw_bars(struct display *d)
{
	static const uint32_t col[] = {
		0xffffff, 0xffff00, 0x00ffff, 0x00ff00,
		0xff00ff, 0xff0000, 0x0000ff, 0x000000,
	};
	uint32_t x, y;

	for (y = 0; y < d->height; y++)
		for (x = 0; x < d->width; x++)
			put(d, x, y, col[x * 8 / d->width]);
}

static void draw_gradient(struct display *d)
{
	uint32_t x, y;

	/* Red ramps left->right, green top->bottom, blue in the bottom half */
	for (y = 0; y < d->height; y++)
		for (x = 0; x < d->width; x++) {
			uint32_t r = x * 255 / (d->width - 1);
			uint32_t g = y * 255 / (d->height - 1);
			uint32_t b = y >= d->height / 2 ? 255 - r : 0;

			put(d, x, y, r << 16 | g << 8 | b);
		}
}

static void draw_grid(struct display *d)
{
	uint32_t x, y;

	for (y = 0; y < d->height; y++)
		for (x = 0; x < d->width; x++) {
			uint32_t c = (x % 20 == 0 || y % 20 == 0) ? 0xffffff
								  : 0x000000;

			if (x == 0 || y == 0 || x == d->width - 1 ||
			    y == d->height - 1)
				c = 0xff0000;	/* border: check nothing is cut */
			put(d, x, y, c);
		}
	/* A green block marks the top-left corner (rotation check) */
	fill_rect(d, 4, 4, 30, 30, 0x00ff00);
}

static void sleep_ms(long ms)
{
	struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };

	nanosleep(&ts, NULL);
}

/* A square bouncing around; each frame only the changed area is flushed. */
static void bounce(struct display *d, int seconds)
{
	const int sz = 40;
	int x = 10, y = 10, dx = 3, dy = 2;
	long frames = 0, total = seconds * 1000L / 16;
	struct timespec t0, t1;
	double secs;

	memset(d->pix, 0, d->size);
	dirty(d, NULL);
	clock_gettime(CLOCK_MONOTONIC, &t0);

	while (frames < total) {
		int nx = x + dx, ny = y + dy;
		struct drm_clip_rect clip;

		if (nx < 0 || nx + sz > (int)d->width) {
			dx = -dx;
			nx = x + dx;
		}
		if (ny < 0 || ny + sz > (int)d->height) {
			dy = -dy;
			ny = y + dy;
		}

		fill_rect(d, x, y, sz, sz, 0x000000);
		fill_rect(d, nx, ny, sz, sz, 0x00a0ff);

		/* Union of the old and new square */
		clip.x1 = nx < x ? nx : x;
		clip.y1 = ny < y ? ny : y;
		clip.x2 = (nx > x ? nx : x) + sz;
		clip.y2 = (ny > y ? ny : y) + sz;
		dirty(d, &clip);

		x = nx;
		y = ny;
		frames++;
		sleep_ms(16);
	}

	clock_gettime(CLOCK_MONOTONIC, &t1);
	secs = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
	printf("%ld frames in %.2f s (%.1f fps)\n", frames, secs,
	       frames / secs);
}

static void destroy_fb(struct display *d)
{
	struct drm_mode_destroy_dumb dreq = { .handle = d->handle };

	munmap(d->pix, d->size);
	xioctl(d->fd, DRM_IOCTL_MODE_RMFB, &d->fb_id);
	xioctl(d->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"usage: %s [-d /dev/dri/cardN] <command> [args]\n"
		"  info                  show the mode and pipeline IDs\n"
		"  bars [sec]            8 colour bars\n"
		"  gradient [sec]        RGB gradient (checks colour depth)\n"
		"  grid [sec]            20 px grid, red border, green corner\n"
		"  fill RRGGBB [sec]     solid colour\n"
		"  bounce [sec]          moving square using damage clips\n",
		prog);
	exit(2);
}

int main(int argc, char **argv)
{
	struct display d = { 0 };
	const char *dev = NULL, *cmd;
	int argi = 1, seconds = 5;

	if (argc > 2 && strcmp(argv[1], "-d") == 0) {
		dev = argv[2];
		argi = 3;
	}
	if (argi >= argc)
		usage(argv[0]);
	cmd = argv[argi++];

	d.fd = open_card(dev);
	find_pipeline(&d);

	if (strcmp(cmd, "info") == 0) {
		printf("mode %s: %ux%u @ %u Hz\n", d.mode.name, d.width,
		       d.height, d.mode.vrefresh);
		printf("connector %u, crtc %u\n", d.conn_id, d.crtc_id);
		return 0;
	}

	create_fb(&d);

	if (strcmp(cmd, "bars") == 0) {
		draw_bars(&d);
	} else if (strcmp(cmd, "gradient") == 0) {
		draw_gradient(&d);
	} else if (strcmp(cmd, "grid") == 0) {
		draw_grid(&d);
	} else if (strcmp(cmd, "fill") == 0) {
		if (argi >= argc)
			usage(argv[0]);
		fill_rect(&d, 0, 0, d.width, d.height,
			  strtoul(argv[argi++], NULL, 16) & 0xffffff);
	} else if (strcmp(cmd, "bounce") != 0) {
		usage(argv[0]);
	}

	if (argi < argc)
		seconds = atoi(argv[argi]);

	set_crtc(&d);	/* the driver sends the full frame on enable */

	if (strcmp(cmd, "bounce") == 0)
		bounce(&d, seconds);
	else
		sleep(seconds);

	destroy_fb(&d);
	close(d.fd);
	return 0;
}
