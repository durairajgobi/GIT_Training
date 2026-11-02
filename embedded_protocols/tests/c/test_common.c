#include "ep/ep_common.h"
#include "ep/ep_crc.h"
#include "ep/ep_ringbuf.h"
#include "ep_test.h"

static const uint8_t check_str[] = "123456789";

static void test_crc_check_values(void)
{
    /* catalogue "check" values (reveng.sourceforge.io) */
    CHECK_EQ(ep_crc8_smbus(0, check_str, 9), 0xF4);
    CHECK_EQ(ep_crc16_modbus(EP_CRC16_MODBUS_INIT, check_str, 9), 0x4B37);
    CHECK_EQ(ep_crc16_ccitt(EP_CRC16_CCITT_INIT, check_str, 9), 0x29B1);
    CHECK_EQ(ep_crc32_ieee(EP_CRC32_INIT, check_str, 9), 0xCBF43926u);
}

static void test_crc_incremental(void)
{
    uint32_t c = ep_crc32_ieee(EP_CRC32_INIT, check_str, 4);
    c = ep_crc32_ieee(c, check_str + 4, 5);
    CHECK_EQ(c, 0xCBF43926u);
    uint16_t m = ep_crc16_modbus(EP_CRC16_MODBUS_INIT, check_str, 3);
    m = ep_crc16_modbus(m, check_str + 3, 6);
    CHECK_EQ(m, 0x4B37);
}

static void test_ringbuf(void)
{
    uint8_t store[8], out[16];
    ep_ringbuf_t rb;
    CHECK_EQ(ep_rb_init(&rb, store, 6), EP_ERR_PARAM); /* not power of 2 */
    CHECK_EQ(ep_rb_init(&rb, store, 8), EP_OK);
    CHECK_EQ(ep_rb_count(&rb), 0);
    const uint8_t in[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    CHECK_EQ(ep_rb_write(&rb, in, 10), 8); /* full capacity usable */
    CHECK_EQ(ep_rb_space(&rb), 0);
    CHECK(!ep_rb_put(&rb, 99));
    CHECK_EQ(ep_rb_read(&rb, out, 3), 3);
    CHECK_MEM(out, in, 3);
    CHECK_EQ(ep_rb_write(&rb, in, 3), 3); /* wraps */
    CHECK_EQ(ep_rb_read(&rb, out, 16), 8);
    const uint8_t expect[8] = {4, 5, 6, 7, 8, 1, 2, 3};
    CHECK_MEM(out, expect, 8);
    uint8_t b;
    CHECK(!ep_rb_get(&rb, &b));
}

static void test_ringbuf_counter_wrap(void)
{
    uint8_t store[4], b;
    ep_ringbuf_t rb;
    ep_rb_init(&rb, store, 4);
    rb.head = rb.tail = SIZE_MAX - 1; /* free-running counters about to wrap */
    for (uint8_t i = 0; i < 10; i++) {
        CHECK(ep_rb_put(&rb, i));
        CHECK(ep_rb_get(&rb, &b));
        CHECK_EQ(b, i);
    }
    CHECK_EQ(ep_rb_count(&rb), 0);
}

static void test_endian(void)
{
    uint8_t b[8];
    ep_put_be48(b, 0x010203040506ull);
    CHECK_EQ(b[0], 1);
    CHECK_EQ(b[5], 6);
    CHECK_EQ(ep_get_be48(b), 0x010203040506ll);
    ep_put_be64(b, 0x1122334455667788ull);
    CHECK(ep_get_be64(b) == 0x1122334455667788ull);
    ep_put_le16(b, 0xABCD);
    CHECK_EQ(b[0], 0xCD);
}

int main(void)
{
    RUN(test_crc_check_values);
    RUN(test_crc_incremental);
    RUN(test_ringbuf);
    RUN(test_ringbuf_counter_wrap);
    RUN(test_endian);
    TEST_MAIN_END();
}
