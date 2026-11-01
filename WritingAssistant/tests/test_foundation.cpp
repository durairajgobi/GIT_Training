// Phase 1: JSON, configuration, and the dependency-injection container.
#include "test_framework.hpp"
#include "wa/config.hpp"
#include "wa/json.hpp"
#include "wa/service_container.hpp"

#include <map>
#include <ostream>

using namespace wa;

namespace wa {
// Lets EXPECT_EQ print Json values on failure.
std::ostream& operator<<(std::ostream& os, const Json& j) { return os << j.dump(); }
}  // namespace wa

// ---- JSON ------------------------------------------------------------------

TEST(json_parses_nested_document) {
    Json j = Json::parse(R"({"a": [1, 2.5, -3e2], "b": {"c": true, "d": null}, "s": "hi"})");
    EXPECT_EQ(j["a"].asArray().size(), size_t(3));
    EXPECT_EQ(j["a"].asArray()[1].asNumber(), 2.5);
    EXPECT_EQ(j["a"].asArray()[2].asNumber(), -300.0);
    EXPECT_TRUE(j["b"]["c"].asBool());
    EXPECT_TRUE(j["b"]["d"].isNull());
    EXPECT_EQ(j.getString("s"), std::string("hi"));
}

TEST(json_round_trips_escapes_and_unicode) {
    Json j;
    j["text"] = std::string("line1\n\"quoted\"\t\\ tab \x01");
    std::string dumped = j.dump();
    EXPECT_CONTAINS(dumped, "\\n");
    EXPECT_CONTAINS(dumped, "\\\"quoted\\\"");
    EXPECT_CONTAINS(dumped, "\\u0001");
    EXPECT_EQ(Json::parse(dumped), j);
}

TEST(json_decodes_unicode_escapes_to_utf8) {
    EXPECT_EQ(Json::parse(R"("café")").asString(), std::string("caf\xC3\xA9"));
    EXPECT_EQ(Json::parse(R"("😀")").asString(), std::string("\xF0\x9F\x98\x80"));
    EXPECT_EQ(Json::parse(R"("வ")").asString(), std::string("\xE0\xAE\xB5"));  // Tamil
}

TEST(json_dumps_integers_without_decimal_point) {
    Json j;
    j["max_tokens"] = 16000;
    EXPECT_EQ(j.dump(), std::string(R"({"max_tokens":16000})"));
}

TEST(json_rejects_malformed_input) {
    EXPECT_THROWS(Json::parse("{"), JsonError);
    EXPECT_THROWS(Json::parse(R"({"a":1,})"), JsonError);
    EXPECT_THROWS(Json::parse("[1 2]"), JsonError);
    EXPECT_THROWS(Json::parse(R"("unterminated)"), JsonError);
    EXPECT_THROWS(Json::parse("tru"), JsonError);
    EXPECT_THROWS(Json::parse("{} extra"), JsonError);
    EXPECT_THROWS(Json::parse(std::string(1000, '[')), JsonError);
}

TEST(json_type_mismatch_throws) {
    Json j = Json::parse("[1]");
    EXPECT_THROWS(j.asString(), JsonError);
    EXPECT_EQ(j.getString("missing", "fb"), std::string("fb"));
}

// ---- Config ----------------------------------------------------------------

TEST(config_reads_environment) {
    std::map<std::string, std::string> env = {
        {"ANTHROPIC_API_KEY", "sk-test"}, {"WA_MODEL", "claude-sonnet-5-5"}, {"WA_EFFORT", "high"},
        {"WA_FALLBACKS", "0"}};
    auto cfg = AppConfig::fromEnvironment([&](const char* n) -> const char* {
        auto it = env.find(n);
        return it == env.end() ? nullptr : it->second.c_str();
    });
    EXPECT_EQ(cfg.apiKey, std::string("sk-test"));
    EXPECT_EQ(cfg.model, std::string("claude-sonnet-5-5"));
    EXPECT_EQ(cfg.effort, std::string("high"));
    EXPECT_FALSE(cfg.serverSideFallbacks);
    EXPECT_EQ(cfg.resolvedProvider(), std::string("anthropic"));
}

TEST(config_defaults_to_offline_without_key) {
    auto cfg = AppConfig::fromEnvironment([](const char*) -> const char* { return nullptr; });
    EXPECT_EQ(cfg.model, std::string("claude-opus-5-5"));
    EXPECT_EQ(cfg.resolvedProvider(), std::string("offline"));
}

// ---- Dependency injection ------------------------------------------------

namespace {
struct IGreeter {
    virtual ~IGreeter() = default;
    virtual std::string greet() const = 0;
};
struct Greeter : IGreeter {
    std::string greet() const override { return "hello"; }
};
struct Counter {
    static inline int built = 0;
    Counter() { ++built; }
};
struct Consumer {
    explicit Consumer(std::shared_ptr<IGreeter> g) : greeter(std::move(g)) {}
    std::shared_ptr<IGreeter> greeter;
};
struct Loop {};
}  // namespace

TEST(di_singleton_is_built_once_and_shared) {
    ServiceContainer c;
    Counter::built = 0;
    c.addSingleton<Counter>([](ServiceContainer&) { return std::make_shared<Counter>(); });
    auto a = c.resolve<Counter>();
    auto b = c.resolve<Counter>();
    EXPECT_TRUE(a == b);
    EXPECT_EQ(Counter::built, 1);
}

TEST(di_transient_is_built_every_time) {
    ServiceContainer c;
    Counter::built = 0;
    c.addTransient<Counter>([](ServiceContainer&) { return std::make_shared<Counter>(); });
    auto a = c.resolve<Counter>();
    auto b = c.resolve<Counter>();
    EXPECT_FALSE(a == b);
    EXPECT_EQ(Counter::built, 2);
}

TEST(di_resolves_interface_and_injects_into_constructor) {
    ServiceContainer c;
    c.addSingleton<IGreeter>([](ServiceContainer&) { return std::make_shared<Greeter>(); });
    c.addTransient<Consumer>([](ServiceContainer& sc) { return std::make_shared<Consumer>(sc.resolve<IGreeter>()); });
    EXPECT_EQ(c.resolve<Consumer>()->greeter->greet(), std::string("hello"));
    EXPECT_TRUE(c.resolve<Consumer>()->greeter == c.resolve<IGreeter>());
}

TEST(di_missing_service_throws) {
    ServiceContainer c;
    EXPECT_FALSE(c.has<IGreeter>());
    EXPECT_THROWS(c.resolve<IGreeter>(), ResolutionError);
}

TEST(di_detects_circular_dependency) {
    ServiceContainer c;
    c.addSingleton<Loop>([](ServiceContainer& sc) { return sc.resolve<Loop>(); });
    EXPECT_THROWS(c.resolve<Loop>(), ResolutionError);
}

TEST(di_registration_can_be_overridden) {
    struct Other : IGreeter {
        std::string greet() const override { return "other"; }
    };
    ServiceContainer c;
    c.addSingleton<IGreeter>([](ServiceContainer&) { return std::make_shared<Greeter>(); });
    c.addInstance<IGreeter>(std::make_shared<Other>());
    EXPECT_EQ(c.resolve<IGreeter>()->greet(), std::string("other"));
}
