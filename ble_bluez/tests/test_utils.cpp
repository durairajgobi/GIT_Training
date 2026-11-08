// SPDX-License-Identifier: MIT
// Host unit tests for the D-Bus-independent helpers.
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#include "ble/utils.hpp"

static int failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

template <typename F>
static bool throwsInvalid(F&& f) {
    try {
        f();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

int main() {
    using namespace ble;

    // Addresses
    CHECK(isValidAddress("AA:BB:CC:DD:EE:FF"));
    CHECK(isValidAddress("aa:bb:cc:dd:ee:0f"));
    CHECK(!isValidAddress("AA:BB:CC:DD:EE"));
    CHECK(!isValidAddress("AA-BB-CC-DD-EE-FF"));
    CHECK(!isValidAddress("GG:BB:CC:DD:EE:FF"));
    CHECK(normalizeAddress("aa:bb:cc:dd:ee:0f") == "AA:BB:CC:DD:EE:0F");
    CHECK(throwsInvalid([] { normalizeAddress("nope"); }));
    CHECK(addressToPathLeaf("aa:bb:cc:dd:ee:ff") == "dev_AA_BB_CC_DD_EE_FF");
    CHECK(devicePath("/org/bluez/hci0", "01:02:03:04:05:06") == "/org/bluez/hci0/dev_01_02_03_04_05_06");

    // UUIDs
    CHECK(normalizeUuid("180F") == "0000180f-0000-1000-8000-00805f9b34fb");
    CHECK(normalizeUuid("0x2a19") == "00002a19-0000-1000-8000-00805f9b34fb");
    CHECK(normalizeUuid("0000FEAA") == "0000feaa-0000-1000-8000-00805f9b34fb");
    CHECK(normalizeUuid("12345678-1234-5678-1234-56789ABCDEF0") == "12345678-1234-5678-1234-56789abcdef0");
    CHECK(throwsInvalid([] { normalizeUuid("18F"); }));
    CHECK(throwsInvalid([] { normalizeUuid("12345678-1234-5678-1234_56789abcdef0"); }));
    CHECK(throwsInvalid([] { normalizeUuid("zzzz"); }));
    CHECK(uuidName("2a19") == "Battery Level");
    CHECK(uuidName("0000180f-0000-1000-8000-00805f9b34fb") == "Battery Service");
    CHECK(uuidName("12345678-1234-5678-1234-56789abcdef0").empty());
    CHECK(uuidName("garbage").empty());

    // Hex
    CHECK((parseHex("0102ff") == std::vector<uint8_t>{0x01, 0x02, 0xff}));
    CHECK((parseHex("01 02 FF") == std::vector<uint8_t>{0x01, 0x02, 0xff}));
    CHECK((parseHex("01:02:ff") == std::vector<uint8_t>{0x01, 0x02, 0xff}));
    CHECK((parseHex("0x0102") == std::vector<uint8_t>{0x01, 0x02}));
    CHECK((parseHex("0x01 0x02") == std::vector<uint8_t>{0x01, 0x02}));
    CHECK((parseHex("00") == std::vector<uint8_t>{0x00}));
    CHECK(parseHex("").empty());
    CHECK(throwsInvalid([] { parseHex("123"); }));
    CHECK(throwsInvalid([] { parseHex("zz"); }));
    CHECK(toHex({0x01, 0xab}) == "01 ab");
    CHECK(toHex({0x01, 0xab}, '\0') == "01ab");
    CHECK(toHex({}) == "");
    CHECK(toPrintable({'h', 'i', 0x00, 0x7f}) == "hi..");

    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("all utils tests passed");
    return 0;
}
