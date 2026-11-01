// test_json.cpp - Minimal self-contained unit tests for sj::Json.
// Build & run:  make test

#include <cmath>
#include <iostream>
#include <string>

#include "json.hpp"

using sj::Json;
using sj::JsonError;

static int g_failed = 0;
static int g_passed = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (cond) {                                                              \
            ++g_passed;                                                          \
        } else {                                                                 \
            ++g_failed;                                                          \
            std::cerr << __FILE__ << ":" << __LINE__ << ": FAILED: " #cond "\n"; \
        }                                                                        \
    } while (0)

#define CHECK_THROWS(expr)                  \
    do {                                    \
        bool thrown = false;                \
        try { (void)(expr); }               \
        catch (const JsonError&) { thrown = true; } \
        CHECK(thrown && #expr);             \
    } while (0)

static void testScalars() {
    CHECK(Json::parse("null").isNull());
    CHECK(Json::parse("true").asBool() == true);
    CHECK(Json::parse("false").asBool() == false);
    CHECK(Json::parse("42").asInt() == 42);
    CHECK(Json::parse("-0.5").asNumber() == -0.5);
    CHECK(Json::parse("1.5e3").asNumber() == 1500);
    CHECK(Json::parse("  \"hi\"  ").asString() == "hi");
}

static void testNumbersDump() {
    CHECK(Json(1).dump() == "1");
    CHECK(Json(-17).dump() == "-17");
    CHECK(Json(0.1).dump() == "0.1");
    CHECK(Json(3.14159).dump() == "3.14159");
    CHECK(Json(1e20).dump() == "1e+20");
    CHECK(Json(std::nan("")).dump() == "null");
}

static void testStrings() {
    Json s = Json::parse(R"("a\"b\\c\/d\n\t\u0041\u00e9\u20ac\ud83d\ude00")");
    CHECK(s.asString() == "a\"b\\c/d\n\tA\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80");
    CHECK(Json("line\nbreak").dump() == R"("line\nbreak")");
    CHECK(Json(std::string("\x01", 1)).dump() == R"("\u0001")");
}

static void testContainers() {
    Json j = Json::parse(R"({"a":[1,2,{"b":null}],"c":{}})");
    CHECK(j.isObject());
    CHECK(j.size() == 2);
    CHECK(j["a"].size() == 3);
    CHECK(j["a"][2]["b"].isNull());
    CHECK(j["c"].isObject() && j["c"].empty());
    CHECK(j.dump() == R"({"a":[1,2,{"b":null}],"c":{}})");
}

static void testKeyOrderPreserved() {
    Json j = Json::parse(R"({"z":1,"a":2,"m":3})");
    CHECK(j.dump() == R"({"z":1,"a":2,"m":3})");
    j["b"] = 4;
    CHECK(j.keys().back() == "b");
}

static void testModify() {
    Json j;
    j["name"] = "x";
    j["list"].push_back(1);
    j["list"].push_back(3);
    j["list"].insert(1, 2);
    CHECK(j["list"].dump() == "[1,2,3]");
    j["list"].eraseAt(0);
    CHECK(j["list"].dump() == "[2,3]");
    CHECK(j.erase("name"));
    CHECK(!j.erase("name"));
    CHECK(!j.contains("name"));
}

static void testPointer() {
    Json j = Json::parse(R"({"a":{"b":[10,20,{"c/d":"x","e~f":"y"}]}})");
    CHECK(j.pointer("")->isObject());
    CHECK(j.pointer("/a/b/1")->asInt() == 20);
    CHECK(j.pointer("/a/b/2/c~1d")->asString() == "x");
    CHECK(j.pointer("/a/b/2/e~0f")->asString() == "y");
    CHECK(j.pointer("/a/b/5") == nullptr);
    CHECK(j.pointer("/nope") == nullptr);
    *j.pointer("/a/b/0") = 99;
    CHECK(j["a"]["b"][0].asInt() == 99);
}

static void testMerge() {
    Json base  = Json::parse(R"({"a":1,"o":{"x":1,"y":2},"arr":[1]})");
    Json patch = Json::parse(R"({"b":2,"o":{"y":3},"arr":[9,9]})");
    base.merge(patch);
    CHECK(base == Json::parse(R"({"a":1,"b":2,"o":{"x":1,"y":3},"arr":[9,9]})"));
}

static void testEquality() {
    CHECK(Json::parse(R"({"a":1,"b":2})") == Json::parse(R"({"b":2,"a":1})"));
    CHECK(Json::parse("[1,2]") != Json::parse("[2,1]"));
}

static void testRoundTrip() {
    const std::string src =
        R"({"k":[true,false,null,1,-2.5,"s",{"n":[]}],"u":"\u00e9"})";
    Json a = Json::parse(src);
    CHECK(Json::parse(a.dump()) == a);
    CHECK(Json::parse(a.dump(2)) == a);
}

static void testErrors() {
    CHECK_THROWS(Json::parse(""));
    CHECK_THROWS(Json::parse("{"));
    CHECK_THROWS(Json::parse("[1,]"));
    CHECK_THROWS(Json::parse("{\"a\":1,}"));
    CHECK_THROWS(Json::parse("01"));
    CHECK_THROWS(Json::parse("1."));
    CHECK_THROWS(Json::parse("-"));
    CHECK_THROWS(Json::parse("tru"));
    CHECK_THROWS(Json::parse("\"abc"));
    CHECK_THROWS(Json::parse("\"\\x\""));
    CHECK_THROWS(Json::parse("\"\\ud800\""));
    CHECK_THROWS(Json::parse("{'a':1}"));
    CHECK_THROWS(Json::parse("[1] 2"));
    CHECK_THROWS(Json::parse(std::string(1000, '[')));
    CHECK_THROWS(Json(5).asString());
    CHECK_THROWS(Json::parse("{}").at("x"));
    CHECK_THROWS(Json::parse("[]").at(0));
}

static void testValueOr() {
    Json j = Json::parse(R"({"n":5,"s":"hi","b":true})");
    CHECK(j.valueOr<int>("n", 0) == 5);
    CHECK(j.valueOr<int>("missing", 7) == 7);
    CHECK(j.valueOr<std::string>("s", "") == "hi");
    CHECK(j.valueOr<std::string>("n", "def") == "def");
    CHECK(j.valueOr<bool>("b", false) == true);
}

int main() {
    testScalars();
    testNumbersDump();
    testStrings();
    testContainers();
    testKeyOrderPreserved();
    testModify();
    testPointer();
    testMerge();
    testEquality();
    testRoundTrip();
    testErrors();
    testValueOr();

    std::cout << g_passed << " passed, " << g_failed << " failed\n";
    return g_failed == 0 ? 0 : 1;
}
