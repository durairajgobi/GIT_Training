// Offline tests - exercise request building, parsing and cost math
// using canned JSON, so no API key or network is needed.
#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>

#include "cost.hpp"
#include "openai_request.hpp"
#include "openai_response.hpp"

using namespace openai;

template <typename E, typename F>
static bool throws(F f) {
    try { f(); } catch (const E&) { return true; }
    return false;
}

int main() {
    // --- Building the request ---
    ResponseRequest req;
    req.input = "He said \"hi\"\nnew line";
    req.max_output_tokens = 100;
    const auto body = build_request_body(req);
    assert(body["model"] == "gpt-4o-mini");
    assert(body["input"] == req.input);
    assert(body["max_output_tokens"] == 100);
    assert(!body.contains("instructions"));
    assert(body.dump().find("\\\"hi\\\"") != std::string::npos);  // escaped

    ResponseRequest bad;
    assert(throws<std::invalid_argument>([&] { build_request_body(bad); }));
    bad.input = "x";
    bad.temperature = 3.0;
    assert(throws<std::invalid_argument>([&] { build_request_body(bad); }));

    // --- Parsing a successful response ---
    const std::string ok = R"({
      "id": "resp_123", "object": "response", "status": "completed",
      "model": "gpt-4o-mini-2024-07-18", "error": null, "incomplete_details": null,
      "output": [{
        "type": "message", "role": "assistant",
        "content": [{"type": "output_text", "text": "Hello from C++!", "annotations": []}]
      }],
      "usage": {"input_tokens": 1000, "input_tokens_details": {"cached_tokens": 200},
                "output_tokens": 500, "output_tokens_details": {"reasoning_tokens": 0},
                "total_tokens": 1500}
    })";
    const auto r = parse_response(200, ok);
    assert(r.text == "Hello from C++!");
    assert(r.status == "completed");
    assert(r.usage.input_tokens == 1000 && r.usage.cached_input_tokens == 200);
    assert(r.usage.output_tokens == 500 && r.usage.total_tokens == 1500);

    // --- Cost: 800*0.15 + 200*0.075 + 500*0.60 = 435 per 1M -> $0.000435 ---
    const auto price = lookup_price(r.model);
    assert(price && price->input_per_million == 0.15);
    assert(std::fabs(estimate_cost_usd(r.usage, *price) - 0.000435) < 1e-12);
    assert(lookup_price("gpt-4.1-mini")->output_per_million == 1.60);
    assert(!lookup_price("unknown-model"));

    // --- Error responses ---
    const std::string err401 = R"({"error":{"message":"Incorrect API key provided",
        "type":"invalid_request_error","param":null,"code":"invalid_api_key"}})";
    try {
        parse_response(401, err401);
        assert(false);
    } catch (const ApiError& e) {
        assert(e.http_status == 401 && e.code == "invalid_api_key" && !e.retryable());
    }
    try {
        parse_response(429, R"({"error":{"message":"Rate limit","type":"requests","code":null}})");
        assert(false);
    } catch (const ApiError& e) {
        assert(e.retryable() && e.code.empty());
    }
    assert(throws<ApiError>([] { parse_response(502, "<html>Bad Gateway</html>"); }));
    assert(throws<ApiError>([] {
        parse_response(200, R"({"status":"completed","output":[]})");
    }));

    // --- Incomplete (hit max_output_tokens) is returned, not thrown ---
    const auto inc = parse_response(200, R"({"status":"incomplete",
        "incomplete_details":{"reason":"max_output_tokens"},"output":[]})");
    assert(inc.incomplete_reason == "max_output_tokens");

    std::cout << "All tests passed\n";
    return 0;
}
