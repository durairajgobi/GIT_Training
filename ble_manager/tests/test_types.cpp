// SPDX-License-Identifier: MIT
#include "blemgr/types.hpp"
#include "test_util.hpp"

using namespace blemgr;

TEST(uuid_short_forms_expand_to_base_uuid) {
    CHECK_EQ(*normalizeUuid("180D"), std::string("0000180d-0000-1000-8000-00805f9b34fb"));
    CHECK_EQ(*normalizeUuid("0x2a37"), std::string("00002a37-0000-1000-8000-00805f9b34fb"));
    CHECK_EQ(*normalizeUuid("0000FEAA"), std::string("0000feaa-0000-1000-8000-00805f9b34fb"));
}

TEST(uuid_128_bit_forms) {
    CHECK_EQ(*normalizeUuid("A0B40001-926D-4D61-98DF-8C5C62EE53B3"),
             std::string("a0b40001-926d-4d61-98df-8c5c62ee53b3"));
    CHECK_EQ(*normalizeUuid("a0b40001926d4d6198df8c5c62ee53b3"),
             std::string("a0b40001-926d-4d61-98df-8c5c62ee53b3"));
}

TEST(uuid_rejects_garbage) {
    CHECK(!normalizeUuid(""));
    CHECK(!normalizeUuid("18g0"));
    CHECK(!normalizeUuid("180"));
    CHECK(!normalizeUuid("a0b40001-926d-4d61-98df_8c5c62ee53b3"));
}

TEST(address_normalisation) {
    CHECK_EQ(*normalizeAddress("aa:bb:cc:dd:ee:0f"), std::string("AA:BB:CC:DD:EE:0F"));
    CHECK_EQ(*normalizeAddress("aa_bb_cc_dd_ee_0f"), std::string("AA:BB:CC:DD:EE:0F"));
    CHECK(!normalizeAddress("aa:bb:cc:dd:ee"));
    CHECK(!normalizeAddress("aa:bb:cc:dd:ee:zz"));
    CHECK(!normalizeAddress("aabb:cc:dd:ee:ff0"));
}

TEST(hex_roundtrip) {
    auto b = parseHex("01:aB ff-00");
    CHECK(b.has_value());
    CHECK_EQ(toHex(*b), std::string("01abff00"));
    CHECK(!parseHex("abc"));
    CHECK(!parseHex("a:b"));
    CHECK(!parseHex("zz"));
    CHECK(parseHex("")->empty());
}

TEST(scan_filter_matching) {
    DeviceInfo d;
    d.address = "AA:BB:CC:DD:EE:FF";
    d.name = "HRM-42";
    d.rssi = -60;
    d.serviceUuids = {*normalizeUuid("180d")};

    ScanFilter f;
    CHECK(f.matches(d));
    f.minRssi = -70;
    CHECK(f.matches(d));
    f.minRssi = -50;
    CHECK(!f.matches(d));
    f.minRssi.reset();
    f.namePrefix = "HRM";
    CHECK(f.matches(d));
    f.namePrefix = "XYZ";
    CHECK(!f.matches(d));
    f.namePrefix.clear();
    f.serviceUuids = {*normalizeUuid("180f")};
    CHECK(!f.matches(d));
    f.serviceUuids.push_back(*normalizeUuid("180d"));
    CHECK(f.matches(d));
    f.addresses = {"11:22:33:44:55:66"};
    CHECK(!f.matches(d));

    ScanFilter r;
    r.minRssi = -90;
    d.rssi.reset();  // cached, not advertising
    CHECK(!r.matches(d));
}

TEST(char_property_flags) {
    auto f = charPropertyFlags(kPropRead | kPropNotify | kPropWriteNoResponse);
    CHECK_EQ(f.size(), size_t(3));
    CHECK_EQ(f[0], std::string("read"));
    CHECK_EQ(f[1], std::string("write-without-response"));
    CHECK_EQ(f[2], std::string("notify"));
}

TEST_MAIN()
