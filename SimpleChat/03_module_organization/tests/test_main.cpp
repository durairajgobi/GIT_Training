// test_main.cpp - Minimal self-contained tests (no framework needed).
// Modules can be tested in isolation because of Phases 2 and 3.
#include <cstdio>
#include <iostream>
#include <string>

#include "chat_app.h"
#include "chat_state.h"
#include "config.h"
#include "intents.h"
#include "json.h"
#include "text_utils.h"

using namespace simplechat;

static int failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            ++failures;                                                          \
            std::cerr << __FILE__ << ":" << __LINE__ << ": FAILED: " #cond "\n"; \
        }                                                                        \
    } while (0)

static void testTextUtils() {
    CHECK(trim("  hi \n") == "hi");
    CHECK(trim("   ").empty());
    CHECK(toLower("HeLLo") == "hello");
    CHECK(startsWith("calc 1 + 2", "calc "));
    CHECK(!startsWith("cal", "calc"));
    CHECK(unquote("\"You> \"") == "You> ");
    CHECK(parseBool("Yes") && parseBool("1") && !parseBool("no"));
}

static void testConfig() {
    Config cfg;
    CHECK(applySetting(cfg, "bot_name", "Ada") && cfg.botName == "Ada");
    CHECK(applySetting(cfg, "max_history", "3") && cfg.maxHistory == 3);
    CHECK(applySetting(cfg, "backend", "CLAUDE") && cfg.backend == "claude");
    CHECK(!applySetting(cfg, "no_such_key", "x"));
    bool threw = false;
    try { applySetting(cfg, "max_tokens", "lots"); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
}

static void testHistoryLimit() {
    Config cfg;
    cfg.maxHistory = 2;
    ChatState st;
    rememberInput(st, cfg, "a");
    rememberInput(st, cfg, "b");
    rememberInput(st, cfg, "c");
    CHECK(st.history.size() == 2 && st.history.front() == "b");
}

static void testIntents() {
    CHECK(calculate(2, '+', 3) == "Result: 5.000000");
    CHECK(calculate(1, '/', 0) == "Cannot divide by zero.");
    CHECK(calculate(1, '%', 2) == "Unknown operator. Use + - * /");

    Config cfg;
    ChatState st;
    std::string reply;
    CHECK(tryRuleReply("My name is Sam", st, cfg, false, reply) && st.userName == "Sam");
    CHECK(tryRuleReply("hello", st, cfg, false, reply) && reply == "Hello again, Sam!");
    // Small talk is skipped in commands-only mode (Claude backend)...
    CHECK(!tryRuleReply("hello", st, cfg, true, reply));
    // ...but commands still run locally.
    CHECK(tryRuleReply("calc 6 * 7", st, cfg, true, reply) && reply == "Result: 42.000000");
    CHECK(tryRuleReply("QUIT", st, cfg, true, reply) && !st.running);
    CHECK(!tryRuleReply("tell me a joke", st, cfg, false, reply));
}

static void testJson() {
    const std::string src =
        R"({"id":"msg_1","content":[{"type":"text","text":"Hi \"there\"\né"}],)"
        R"("usage":{"input_tokens":12},"ok":true,"none":null})";
    json::Value v = json::parse(src);
    CHECK(v["id"].asString() == "msg_1");
    CHECK(v["content"].items().size() == 1);
    CHECK(v["content"].items()[0]["text"].asString() == "Hi \"there\"\n\xC3\xA9");
    CHECK(v["usage"]["input_tokens"].asNumber() == 12);
    CHECK(v["ok"].asBool() && v["none"].isNull() && v["missing"].isNull());
    // Round trip: dump -> parse -> dump is stable.
    CHECK(json::parse(v.dump()).dump() == v.dump());

    bool threw = false;
    try { json::parse("{\"a\":}"); } catch (const json::ParseError&) { threw = true; }
    CHECK(threw);
}

static void testChatAppRulesBackend() {
    Config cfg;
    cfg.logEnabled = false;
    cfg.backend = "rules";
    ChatApp app(cfg);
    CHECK(app.reply("what is your name") == "I'm SimpleBot.");
    CHECK(app.reply("something random") == cfg.fallbackReply);
    app.reply("bye");
    CHECK(!app.running());
}

int main() {
    testTextUtils();
    testConfig();
    testHistoryLimit();
    testIntents();
    testJson();
    testChatAppRulesBackend();
    if (failures == 0) std::cout << "All tests passed.\n";
    return failures == 0 ? 0 : 1;
}
