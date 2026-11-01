// Offline tests - exercise request building, parsing and cost math
// using canned JSON, so no API key or network is needed.
#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>

#include "claude_request.hpp"
#include "claude_response.hpp"
#include "cost.hpp"

using namespace claude;

template <typename E, typename F>
static bool throws(F f) {
    try { f(); } catch (const E&) { return true; }
    return false;
}

int main() {
    // --- Building the request ---
    MessageRequest req;
    req.system = "Be brief.";
    req.messages.push_back({"user", "He said \"hi\"\nnew line"});
    req.effort = "low";
    const auto body = build_request_body(req);
    assert(body["model"] == "claude-opus-5-5");
    assert(body["max_tokens"] == 16000);
    assert(body["system"] == "Be brief.");
    assert(body["messages"][0]["role"] == "user");
    assert(body["messages"][0]["content"] == "He said \"hi\"\nnew line");
    assert(body["output_config"]["effort"] == "low");
    assert(body["fallbacks"] == "default");
    assert(!body.contains("thinking") && !body.contains("temperature"));
    assert(request_headers(req).at(0) == "anthropic-beta: server-side-fallback-2026-07-01");

    const auto ct = build_count_tokens_body(req);
    assert(!ct.contains("max_tokens") && !ct.contains("fallbacks"));

    MessageRequest no_fb = req;
    no_fb.fallbacks_default = false;
    assert(!build_request_body(no_fb).contains("fallbacks"));
    assert(request_headers(no_fb).empty());

    MessageRequest bad;
    assert(throws<std::invalid_argument>([&] { build_request_body(bad); }));  // no messages
    bad.messages = {{"user", "q"}, {"assistant", "prefill"}};
    assert(throws<std::invalid_argument>([&] { build_request_body(bad); }));  // ends with assistant
    bad.messages = {{"user", "q"}};
    bad.effort = "extreme";
    assert(throws<std::invalid_argument>([&] { build_request_body(bad); }));

    // --- Parsing a successful response (thinking block is skipped) ---
    const std::string ok = R"({
      "id": "msg_123", "type": "message", "role": "assistant", "model": "claude-opus-5-5",
      "content": [
        {"type": "thinking", "thinking": "", "signature": "abc"},
        {"type": "text", "text": "Hello from C++!"}
      ],
      "stop_reason": "end_turn", "stop_sequence": null, "stop_details": null,
      "usage": {"input_tokens": 1000, "cache_creation_input_tokens": 2000,
                "cache_read_input_tokens": 10000, "output_tokens": 500}
    })";
    const auto r = parse_response(200, ok);
    assert(r.text == "Hello from C++!");
    assert(r.stop_reason == "end_turn" && !r.truncated() && !r.served_by_fallback);
    assert(r.usage.total_input_tokens() == 13000);

    // --- Cost (Opus 5.5: $4 in, $5 cache write, $0.20 cache read, $20 out per 1M)
    // 1000*4 + 2000*5 + 10000*0.20 + 500*20 = 26000 per 1M -> $0.026
    const auto price = lookup_price(r.model);
    assert(price && price->input_per_million == 4.00);
    assert(std::fabs(estimate_cost_usd(r.usage, *price) - 0.026) < 1e-12);
    assert(lookup_price("claude-opus-5")->input_per_million == 5.00);  // not 5-5
    assert(!lookup_price("gpt-4o"));

    // --- Truncated response is returned, not thrown ---
    const auto cut = parse_response(200, R"({"model":"claude-opus-5-5",
        "content":[{"type":"text","text":"partial"}],"stop_reason":"max_tokens",
        "usage":{"input_tokens":5,"output_tokens":10}})");
    assert(cut.truncated() && cut.text == "partial");

    // --- Fallback detection ---
    const auto fb = parse_response(200, R"({"model":"claude-opus-4-8",
        "content":[{"type":"fallback","from":{"model":"claude-opus-5-5"},"to":{"model":"claude-opus-4-8"}},
                   {"type":"text","text":"answer"}],
        "stop_reason":"end_turn",
        "usage":{"input_tokens":5,"output_tokens":3,
                 "iterations":[{"type":"message"},{"type":"fallback_message"}]}})");
    assert(fb.served_by_fallback && fb.text == "answer");

    // --- Refusal: HTTP 200 but must not be treated as an answer ---
    try {
        parse_response(200, R"({"content":[],"stop_reason":"refusal",
            "stop_details":{"type":"refusal","category":"cyber"}})");
        assert(false);
    } catch (const ApiError& e) {
        assert(e.type == "refusal" && !e.retryable());
        assert(std::string(e.what()).find("cyber") != std::string::npos);
    }

    // --- Error responses ---
    try {
        parse_response(401, R"({"type":"error","error":{"type":"authentication_error",
            "message":"invalid x-api-key"}})");
        assert(false);
    } catch (const ApiError& e) {
        assert(e.http_status == 401 && e.type == "authentication_error" && !e.retryable());
    }
    try {
        parse_response(529, R"({"type":"error","error":{"type":"overloaded_error","message":"Overloaded"}})");
        assert(false);
    } catch (const ApiError& e) {
        assert(e.retryable());
    }
    assert(throws<ApiError>([] { parse_response(502, "<html>Bad Gateway</html>"); }));
    assert(throws<ApiError>([] {
        parse_response(200, R"({"content":[],"stop_reason":"end_turn"})");
    }));

    // --- count_tokens ---
    assert(parse_count_tokens(200, R"({"input_tokens": 42})") == 42);
    assert(throws<ApiError>([] { parse_count_tokens(200, R"({})"); }));

    std::cout << "All tests passed\n";
    return 0;
}
