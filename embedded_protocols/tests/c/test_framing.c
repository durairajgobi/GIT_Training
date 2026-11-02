#include "ep/ep_framing.h"
#include "ep_test.h"

static void cobs_vector(const uint8_t *in, size_t n, const uint8_t *exp, size_t en)
{
    uint8_t enc[300], dec[300];
    size_t e = ep_cobs_encode(in, n, enc, sizeof(enc));
    CHECK_EQ(e, en);
    CHECK_MEM(enc, exp, en);
    for (size_t i = 0; i < e; i++)
        CHECK(enc[i] != 0);
    CHECK_EQ(ep_cobs_decode(enc, e, dec, sizeof(dec)), (long)n);
    CHECK_MEM(dec, in, n);
}

static void test_cobs_vectors(void)
{
    /* vectors from Cheshire & Baker / Wikipedia */
    const uint8_t a[] = {0x00}, ea[] = {0x01, 0x01};
    const uint8_t b[] = {0x00, 0x00}, eb[] = {0x01, 0x01, 0x01};
    const uint8_t c[] = {0x11, 0x22, 0x00, 0x33}, ec[] = {0x03, 0x11, 0x22, 0x02, 0x33};
    const uint8_t d[] = {0x11, 0x22, 0x33, 0x44}, ed[] = {0x05, 0x11, 0x22, 0x33, 0x44};
    const uint8_t e[] = {0x11, 0x00, 0x00, 0x00}, ee[] = {0x02, 0x11, 0x01, 0x01, 0x01};
    cobs_vector(a, 1, ea, 2);
    cobs_vector(b, 2, eb, 3);
    cobs_vector(c, 4, ec, 5);
    cobs_vector(d, 4, ed, 5);
    cobs_vector(e, 4, ee, 5);

    uint8_t run[254], exp[255];
    for (int i = 0; i < 254; i++)
        run[i] = (uint8_t)(i + 1);
    exp[0] = 0xFF;
    memcpy(exp + 1, run, 254);
    cobs_vector(run, 254, exp, 255); /* canonical: no trailing 0x01 */

    uint8_t run0[255], exp0[257];
    memcpy(run0, run, 254);
    run0[254] = 0x00;
    memcpy(exp0, exp, 255);
    exp0[255] = 0x01;
    exp0[256] = 0x01;
    cobs_vector(run0, 255, exp0, 257);
}

static void test_cobs_overflow_and_garbage(void)
{
    uint8_t in[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10}, out[10];
    CHECK_EQ(ep_cobs_encode(in, 10, out, 10), 0); /* needs 11 */
    const uint8_t bad[] = {0x05, 0x11, 0x22}; /* truncated block */
    CHECK_EQ(ep_cobs_decode(bad, 3, out, sizeof(out)), -1);
}

static void test_frame_stream(void)
{
    const uint8_t p1[] = {0x00, 0x01, 0x00, 0xFF, 0x7E};
    const uint8_t p2[] = "telemetry";
    uint8_t wire[64];
    size_t n = 0;
    wire[n++] = 0x42; /* line noise before sync */
    wire[n++] = 0x17;
    wire[n++] = 0x00;
    n += ep_frame_encode(p1, sizeof(p1), wire + n, sizeof(wire) - n);
    n += ep_frame_encode(p2, 9, wire + n, sizeof(wire) - n);

    uint8_t acc[64], out[64];
    ep_frame_decoder_t d;
    ep_frame_decoder_init(&d, acc, sizeof(acc));
    int frames = 0, bad = 0;
    for (size_t i = 0; i < n; i++) {
        size_t len;
        ep_status_t st = ep_frame_decoder_feed(&d, wire[i], out, sizeof(out), &len);
        if (st == EP_OK) {
            if (frames == 0) {
                CHECK_EQ(len, sizeof(p1));
                CHECK_MEM(out, p1, len);
            } else {
                CHECK_EQ(len, 9);
                CHECK_MEM(out, p2, 9);
            }
            frames++;
        } else if (st != EP_ERR_AGAIN) {
            bad++;
        }
    }
    CHECK_EQ(frames, 2);
    CHECK_EQ(bad, 1); /* the noise "frame" is rejected, then resync */
}

static void test_frame_crc_error(void)
{
    const uint8_t p[] = {1, 2, 3, 4};
    uint8_t wire[16], acc[16], out[16];
    size_t n = ep_frame_encode(p, 4, wire, sizeof(wire));
    wire[2] ^= 0x40; /* flip a payload bit (never creates a 0x00) */
    ep_frame_decoder_t d;
    ep_frame_decoder_init(&d, acc, sizeof(acc));
    ep_status_t st = EP_ERR_AGAIN;
    size_t len;
    for (size_t i = 0; i < n; i++)
        st = ep_frame_decoder_feed(&d, wire[i], out, sizeof(out), &len);
    CHECK_EQ(st, EP_ERR_CRC);
    CHECK_EQ(d.crc_errors, 1);
}

static void test_frame_decoder_overflow(void)
{
    uint8_t acc[4], out[16];
    ep_frame_decoder_t d;
    ep_frame_decoder_init(&d, acc, sizeof(acc));
    size_t len;
    for (int i = 1; i <= 8; i++)
        ep_frame_decoder_feed(&d, (uint8_t)i, out, sizeof(out), &len);
    CHECK_EQ(ep_frame_decoder_feed(&d, 0, out, sizeof(out), &len), EP_ERR_OVERFLOW);
}

int main(void)
{
    RUN(test_cobs_vectors);
    RUN(test_cobs_overflow_and_garbage);
    RUN(test_frame_stream);
    RUN(test_frame_crc_error);
    RUN(test_frame_decoder_overflow);
    TEST_MAIN_END();
}
