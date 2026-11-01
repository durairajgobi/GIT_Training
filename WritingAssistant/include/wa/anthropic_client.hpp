// ILLMClient implementation that calls the Claude Messages API over raw HTTP.
#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>

#include "wa/http_client.hpp"
#include "wa/json.hpp"
#include "wa/llm_client.hpp"

namespace wa {

struct AnthropicOptions {
    std::string apiKey;
    std::string model = "claude-opus-5-5";
    std::string baseUrl = "https://api.anthropic.com";
    std::string apiVersion = "2026-10-21";
    std::string effort = "medium";       // low | medium | high | xhigh | max
    bool serverSideFallbacks = true;     // retry refusals on a fallback model, server side
    int maxRetries = 2;                  // for 408/409/429/5xx and transport errors
    std::chrono::milliseconds retryBaseDelay{1000};
    int timeoutSeconds = 600;
};

class AnthropicClient : public ILLMClient {
public:
    using Sleeper = std::function<void(std::chrono::milliseconds)>;

    AnthropicClient(std::shared_ptr<IHttpClient> http, AnthropicOptions options, Sleeper sleeper = {});

    CompletionResult complete(const CompletionRequest& request) override;
    std::string name() const override { return "anthropic:" + options_.model; }

    // Exposed for tests.
    Json buildRequestBody(const CompletionRequest& request) const;
    static CompletionResult parseResponse(const std::string& body);

private:
    static bool isRetryable(int status);

    std::shared_ptr<IHttpClient> http_;
    AnthropicOptions options_;
    Sleeper sleep_;
};

}  // namespace wa
