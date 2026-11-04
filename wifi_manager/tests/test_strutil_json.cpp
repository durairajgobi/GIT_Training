#include "test.hpp"
#include "wifimgr/json.hpp"
#include "wifimgr/strutil.hpp"

using namespace wifimgr;

TEST(wpa_decode_handles_escapes) {
  CHECK_EQ(wpaDecode("Caf\\xc3\\xa9"), std::string("Caf\xc3\xa9"));
  CHECK_EQ(wpaDecode("a\\\"b\\\\c"), std::string("a\"b\\c"));
  CHECK_EQ(wpaDecode("tab\\there"), std::string("tab\there"));
  CHECK_EQ(wpaDecode("bad\\xZZ"), std::string("bad\\xZZ"));
  CHECK_EQ(wpaDecode("trail\\"), std::string("trail\\"));
}

TEST(c_escape_round_trips_arbitrary_bytes) {
  std::string all;
  for (int i = 0; i < 256; ++i) all.push_back(static_cast<char>(i));
  std::string esc = cEscape(all);
  CHECK(esc.find('\n') == std::string::npos);
  auto back = cUnescape(esc);
  REQUIRE(back.has_value());
  CHECK(*back == all);
  CHECK(!cUnescape("bad\\q").has_value());
  CHECK(!cUnescape("short\\x4").has_value());
  CHECK(!cUnescape("end\\").has_value());
}

TEST(hex_encode_decode) {
  CHECK_EQ(hexEncode("Hi\x01"), std::string("486901"));
  CHECK_EQ(*hexDecode("486901"), std::string("Hi\x01"));
  CHECK(!hexDecode("abc").has_value());
  CHECK(!hexDecode("zz").has_value());
}

TEST(tokenize_quotes_and_escapes) {
  auto t = tokenize(R"(connect "My Net" psk "pa\"ss word" priority 5)");
  REQUIRE(t.has_value());
  REQUIRE(t->size() == 6);
  CHECK_EQ((*t)[1], std::string("My Net"));
  CHECK_EQ((*t)[3], std::string("pa\"ss word"));
  CHECK(!tokenize("connect \"open").has_value());
  auto empty = tokenize(R"(add "")");
  REQUIRE(empty && empty->size() == 2);
  CHECK((*empty)[1].empty());
}

TEST(quote_arg_is_inverse_of_tokenize) {
  for (std::string s : {std::string("plain"), std::string("with space"), std::string("q\"uote"),
                        std::string("back\\slash"), std::string("")}) {
    auto t = tokenize("cmd " + quoteArg(s));
    REQUIRE(t && t->size() == 2);
    CHECK_EQ((*t)[1], s);
  }
}

TEST(parse_int_and_bool) {
  CHECK_EQ(*parseInt(" -42 "), -42L);
  CHECK(!parseInt("4x").has_value());
  CHECK(!parseInt("").has_value());
  bool b = false;
  CHECK(parseBool("on", &b) && b);
  CHECK(parseBool("false", &b) && !b);
  CHECK(!parseBool("maybe", &b));
}

TEST(json_dump_and_parse_round_trip) {
  Json j = Json::object()
               .set("s", "a\"b\n")
               .set("n", -55)
               .set("f", 1.5)
               .set("b", true)
               .set("z", nullptr)
               .set("arr", Json::array().push(1).push("x"));
  std::string text = j.dump();
  CHECK_EQ(text, std::string(R"({"s":"a\"b\n","n":-55,"f":1.5,"b":true,"z":null,"arr":[1,"x"]})"));
  auto p = Json::parse(text);
  REQUIRE(p.has_value());
  CHECK_EQ(p->getString("s"), std::string("a\"b\n"));
  CHECK_EQ(p->getInt("n"), -55L);
  CHECK(p->getBool("b"));
  CHECK_EQ(p->get("arr")->items().size(), size_t(2));
}

TEST(json_non_utf8_bytes_survive_round_trip) {
  std::string ssid = "x\xff\xfe" "y\xc3\xa9";  // invalid bytes + valid UTF-8
  std::string text = Json(ssid).dump();
  CHECK(text.find("\\u00ff") != std::string::npos);
  CHECK(text.find("\xc3\xa9") != std::string::npos);
  auto back = Json::parse(text);
  REQUIRE(back.has_value());
  CHECK(back->asString() == ssid);
}

TEST(json_rejects_garbage) {
  CHECK(!Json::parse("{").has_value());
  CHECK(!Json::parse("{\"a\":1,}").has_value());
  CHECK(!Json::parse("[1 2]").has_value());
  CHECK(!Json::parse("\"abc").has_value());
  CHECK(!Json::parse("{} extra").has_value());
  std::string deep(200, '[');
  CHECK(!Json::parse(deep).has_value());
}
