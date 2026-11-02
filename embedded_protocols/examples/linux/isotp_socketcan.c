/*
 * isotp_socketcan - UDS "ReadDataByIdentifier VIN" (0x22 F190) over ISO-TP
 * using the portable ep_isotp engine on top of SocketCAN.
 *
 *   sudo ip link set can0 up type can bitrate 500000
 *   ./isotp_socketcan can0            # tester 0x7E0 -> ECU 0x7E8
 *
 * Try it without hardware:
 *   sudo modprobe vcan && sudo ip link add vcan0 type vcan && sudo ip link set vcan0 up
 */
#include "ep/ep_isotp.h"
#include "ep_linux.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    ep_linux_can_t can;
    bool done;
    uint8_t resp[EP_ISOTP_MAX_LEN];
    size_t resp_len;
} app_t;

static ep_status_t tx(void *arg, const ep_can_frame_t *f)
{
    return ep_linux_can_send(&((app_t *)arg)->can, f);
}

static void rx(void *arg, const uint8_t *d, size_t n)
{
    app_t *a = arg;
    if (n >= 1 && d[0] == 0x7F && n >= 3 && d[2] == 0x78)
        return; /* NRC 0x78 responsePending: keep waiting */
    memcpy(a->resp, d, n);
    a->resp_len = n;
    a->done = true;
}

int main(int argc, char **argv)
{
    const char *ifname = argc > 1 ? argv[1] : "can0";
    static app_t app;
    if (ep_linux_can_open(&app.can, ifname, false, false) != EP_OK) {
        perror(ifname);
        return 1;
    }
    ep_can_filter_t flt = {.id = 0x7E8, .mask = 0x7FF, .ext = false};
    ep_linux_can_set_filters(&app.can, &flt, 1);

    ep_isotp_config_t cfg;
    ep_isotp_config_default(&cfg);
    cfg.tx_id = 0x7E0;
    cfg.rx_id = 0x7E8;
    cfg.send = tx;
    cfg.on_rx = rx;
    cfg.arg = &app;
    static uint8_t rxbuf[EP_ISOTP_MAX_LEN];
    cfg.rx_buf = rxbuf;
    cfg.rx_cap = sizeof(rxbuf);
    ep_isotp_t tp;
    ep_isotp_init(&tp, &cfg);

    const ep_clock_t *clk = ep_linux_clock();
    static const uint8_t req[] = {0x22, 0xF1, 0x90};
    uint64_t start = ep_clock_now(clk);
    ep_isotp_send(&tp, req, sizeof(req), start);

    while (!app.done && ep_clock_now(clk) - start < 2000000000ull) {
        ep_can_frame_t f;
        if (ep_linux_can_recv(&app.can, &f, 1) == EP_OK)
            ep_isotp_on_frame(&tp, &f, ep_clock_now(clk));
        ep_isotp_poll(&tp, ep_clock_now(clk));
    }
    if (!app.done) {
        fprintf(stderr, "no response from ECU\n");
        return 1;
    }
    if (app.resp_len >= 3 && app.resp[0] == 0x62) {
        printf("VIN: %.*s\n", (int)(app.resp_len - 3), (const char *)app.resp + 3);
    } else if (app.resp_len >= 3 && app.resp[0] == 0x7F) {
        printf("negative response, NRC 0x%02X\n", app.resp[2]);
    }
    ep_linux_can_close(&app.can);
    return 0;
}
