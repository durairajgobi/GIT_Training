// AnthropicClient against a fake HTTP transport: request shape, response
// parsing, errors, refusals, and retries.
#include "fakes.hpp"
#include "test_framework.hpp"
#include "wa/anthropic_client.hpp"

using namespace wa;

namespace {

AnthropicOptions testOptions() {
    AnthropicOptions o;
    o.apiKey = "sk-test";
    o.retryBaseDelay = std::chrono::milliseconds(0);
    return o;
}

std::shared_ptr<AnthropicClient> makeClient(std::shared_ptr<wt::FakeHttpClient> http,
                                            AnthropicOptions o = testOptions()) {
    return std::make_shared<AnthropicClient>(http, o, [](std::chrono::milliseconds) {});
}

CompletionRequest sampleRequest() {
    CompletionRequest r;
    r.task = "summarize";
    r.systemPrompt = "Be brief.";
    r.userPrompt = "Summarize: \"quoted\"\nnext line";
    r.maxTokens = 4000;
    return r;
}

const char* kOk = R"({
  "id": "msg_1", "type": "message", "role": "assistant", "model": "claude-opus-5-5",
  "content": [
    {"type": "thinking", "thinking": "", "signature": "abc"},
    {"type": "text", "text": "Hello, "},
    {"type": "text", "text": "world"}
  ],
  "stop_reason": "end_turn", "stop_details": null,
  "usage": {"input_tokens": 12, "output_tokens": 5}
})";

}  // namespace

TEST(anthropic_requires_api_key) {
    AnthropicOptions o;
    EXPECT_THROWS(AnthropicClient(std::make_shared<wt::FakeHttpClient>(), o), std::invalid_argument);
}

TEST(anthropic_sends_messages_request) {
    auto http = std::make_shared<wt::FakeHttpClient>();
    http->responses.push_back({200, kOk});
    auto r = makeClient(http)->complete(sampleRequest());
    EXPECT_EQ(r.text, std::string("Hello, world"));

    EXPECT_EQ(http->requests.size(), size_t(1));
    const auto& req = http->requests[0];
    EXPECT_EQ(req.url, std::string("https://api.anthropic.com/v1/messages"));
    EXPECT_EQ(req.method, std::string("POST"));
    EXPECT_EQ(req.headers.at("x-api-key"), std::string("sk-test"));
    EXPECT_EQ(req.headers.at("anthropic-version"), std::string("2026-10-21"));
    EXPECT_EQ(req.headers.at("anthropic-beta"), std::string("server-side-fallback-2026-07-01"));
    EXPECT_EQ(req.headers.at("content-type"), std::string("application/json"));

    Json body = Json::parse(req.body);
    EXPECT_EQ(body.getString("model"), std::string("claude-opus-5-5"));
    EXPECT_EQ(body["max_tokens"].asNumber(), 4000.0);
    EXPECT_EQ(body.getString("system"), std::string("Be brief."));
    EXPECT_EQ(body["messages"].asArray()[0].getString("role"), std::string("user"));
    EXPECT_EQ(body["messages"].asArray()[0].getString("content"), sampleRequest().userPrompt);
    EXPECT_EQ(body["output_config"].getString("effort"), std::string("medium"));
    EXPECT_EQ(body.getString("fallbacks"), std::string("default"));
    // Not accepted on API version 2026-10-21 / current models.
    EXPECT_FALSE(body.contains("temperature"));
    EXPECT_FALSE(body.contains("thinking"));
}

TEST(anthropic_can_disable_fallbacks) {
    auto http = std::make_shared<wt::FakeHttpClient>();
    http->responses.push_back({200, kOk});
    auto o = testOptions();
    o.serverSideFallbacks = false;
    makeClient(http, o)->complete(sampleRequest());
    EXPECT_FALSE(http->requests[0].headers.count("anthropic-beta") > 0);
    EXPECT_FALSE(Json::parse(http->requests[0].body).contains("fallbacks"));
}

TEST(anthropic_parses_usage_and_stop_reason) {
    auto r = AnthropicClient::parseResponse(kOk);
    EXPECT_EQ(r.inputTokens, 12);
    EXPECT_EQ(r.outputTokens, 5);
    EXPECT_EQ(r.stopReason, std::string("end_turn"));
    EXPECT_EQ(r.model, std::string("claude-opus-5-5"));
}

TEST(anthropic_flags_truncated_output) {
    auto r = AnthropicClient::parseResponse(
        R"({"content":[{"type":"text","text":"partial"}],"stop_reason":"max_tokens"})");
    EXPECT_CONTAINS(r.text, "partial");
    EXPECT_CONTAINS(r.text, "truncated");
}

TEST(anthropic_refusal_raises_refusal_error) {
    auto http = std::make_shared<wt::FakeHttpClient>();
    http->responses.push_back({200, R"({"content":[],"stop_reason":"refusal",
        "stop_details":{"type":"refusal","category":"cyber","explanation":"not allowed"}})"});
    try {
        makeClient(http)->complete(sampleRequest());
        WT_FAIL("expected RefusalError");
    } catch (const RefusalError& e) {
        EXPECT_EQ(e.category(), std::string("cyber"));
        EXPECT_CONTAINS(e.what(), "not allowed");
    }
}

TEST(anthropic_reports_api_error_without_retrying_4xx) {
    auto http = std::make_shared<wt::FakeHttpClient>();
    http->responses.push_back(
        {400, R"({"type":"error","error":{"type":"invalid_request_error","message":"bad field"}})"});
    try {
        makeClient(http)->complete(sampleRequest());
        WT_FAIL("expected LLMError");
    } catch (const LLMError& e) {
        EXPECT_EQ(e.status(), 400);
        EXPECT_CONTAINS(e.what(), "invalid_request_error: bad field");
    }
    EXPECT_EQ(http->requests.size(), size_t(1));
}

TEST(anthropic_retries_overloaded_then_succeeds) {
    auto http = std::make_shared<wt::FakeHttpClient>();
    http->responses.push_back({529, R"({"type":"error","error":{"type":"overloaded_error","message":"busy"}})"});
    http->responses.push_back({429, "rate limited"});
    http->responses.push_back({200, kOk});
    EXPECT_EQ(makeClient(http)->complete(sampleRequest()).text, std::string("Hello, world"));
    EXPECT_EQ(http->requests.size(), size_t(3));
}

TEST(anthropic_gives_up_after_max_retries) {
    auto http = std::make_shared<wt::FakeHttpClient>();
    for (int i = 0; i < 5; ++i) http->responses.push_back({503, "unavailable"});
    EXPECT_THROWS(makeClient(http)->complete(sampleRequest()), LLMError);
    EXPECT_EQ(http->requests.size(), size_t(3));  // 1 try + 2 retries
}

TEST(anthropic_retries_transport_errors) {
    auto http = std::make_shared<wt::FakeHttpClient>();
    http->transportFailures = 1;
    http->responses.push_back({200, kOk});
    EXPECT_EQ(makeClient(http)->complete(sampleRequest()).text, std::string("Hello, world"));

    auto failing = std::make_shared<wt::FakeHttpClient>();
    failing->transportFailures = 10;
    EXPECT_THROWS(makeClient(failing)->complete(sampleRequest()), TransportError);
}

TEST(anthropic_uses_exponential_backoff) {
    auto http = std::make_shared<wt::FakeHttpClient>();
    for (int i = 0; i < 3; ++i) http->responses.push_back({500, ""});
    auto o = testOptions();
    o.retryBaseDelay = std::chrono::milliseconds(100);
    std::vector<long> delays;
    AnthropicClient client(http, o, [&](std::chrono::milliseconds d) { delays.push_back((long)d.count()); });
    EXPECT_THROWS(client.complete(sampleRequest()), LLMError);
    EXPECT_EQ(delays.size(), size_t(2));
    EXPECT_EQ(delays[0], 100L);
    EXPECT_EQ(delays[1], 200L);
}
