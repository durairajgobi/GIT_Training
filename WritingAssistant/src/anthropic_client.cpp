#include "wa/anthropic_client.hpp"

#include <thread>

namespace wa {

AnthropicClient::AnthropicClient(std::shared_ptr<IHttpClient> http, AnthropicOptions options, Sleeper sleeper)
    : http_(std::move(http)), options_(std::move(options)), sleep_(std::move(sleeper)) {
    if (!http_) throw std::invalid_argument("AnthropicClient requires an IHttpClient");
    if (options_.apiKey.empty()) {
        throw std::invalid_argument("ANTHROPIC_API_KEY is not set (use --provider offline to run without it)");
    }
    if (!sleep_) sleep_ = [](std::chrono::milliseconds d) { std::this_thread::sleep_for(d); };
}

Json AnthropicClient::buildRequestBody(const CompletionRequest& request) const {
    Json body;
    body["model"] = options_.model;
    body["max_tokens"] = request.maxTokens;
    if (!request.systemPrompt.empty()) body["system"] = request.systemPrompt;

    Json message;
    message["role"] = "user";
    message["content"] = request.userPrompt;
    body["messages"] = Json::Array{message};

    // Claude Opus 5.5 always uses adaptive thinking; effort sets how much it thinks.
    if (!options_.effort.empty()) body["output_config"]["effort"] = options_.effort;
    if (options_.serverSideFallbacks) body["fallbacks"] = "default";
    return body;
}

CompletionResult AnthropicClient::parseResponse(const std::string& raw) {
    Json resp = Json::parse(raw);
    CompletionResult result;
    result.model = resp.getString("model");
    result.stopReason = resp.getString("stop_reason");

    if (result.stopReason == "refusal") {
        std::string category, explanation;
        if (const Json* d = resp.find("stop_details"); d && d->isObject()) {
            category = d->getString("category");
            explanation = d->getString("explanation");
        }
        throw RefusalError("The model declined this request" +
                               (explanation.empty() ? std::string() : ": " + explanation),
                           category);
    }

    if (const Json* content = resp.find("content"); content && content->isArray()) {
        for (const auto& block : content->asArray()) {
            // Skip thinking blocks and anything else that is not visible text.
            if (block.getString("type") == "text") result.text += block.getString("text");
        }
    }
    if (const Json* usage = resp.find("usage"); usage && usage->isObject()) {
        if (const Json* v = usage->find("input_tokens"); v && v->isNumber()) result.inputTokens = (int)v->asNumber();
        if (const Json* v = usage->find("output_tokens"); v && v->isNumber()) result.outputTokens = (int)v->asNumber();
    }
    if (result.stopReason == "max_tokens") result.text += "\n[output truncated: max_tokens reached]";
    return result;
}

bool AnthropicClient::isRetryable(int status) {
    return status == 408 || status == 409 || status == 429 || status >= 500;
}

CompletionResult AnthropicClient::complete(const CompletionRequest& request) {
    HttpRequest http;
    http.method = "POST";
    http.url = options_.baseUrl + "/v1/messages";
    http.timeoutSeconds = options_.timeoutSeconds;
    http.headers["content-type"] = "application/json";
    http.headers["x-api-key"] = options_.apiKey;
    http.headers["anthropic-version"] = options_.apiVersion;
    if (options_.serverSideFallbacks) http.headers["anthropic-beta"] = "server-side-fallback-2026-07-01";
    http.body = buildRequestBody(request).dump();

    for (int attempt = 0;; ++attempt) {
        bool last = attempt >= options_.maxRetries;
        auto delay = options_.retryBaseDelay * (1 << attempt);
        HttpResponse resp;
        try {
            resp = http_->send(http);
        } catch (const TransportError&) {
            if (last) throw;
            sleep_(delay);
            continue;
        }

        if (resp.status == 200) return parseResponse(resp.body);

        if (isRetryable(resp.status) && !last) {
            sleep_(delay);
            continue;
        }

        std::string message = "HTTP " + std::to_string(resp.status);
        try {
            Json err = Json::parse(resp.body);
            if (const Json* e = err.find("error"); e && e->isObject()) {
                message += " " + e->getString("type") + ": " + e->getString("message");
            }
        } catch (const JsonError&) {
            if (!resp.body.empty()) message += ": " + resp.body.substr(0, 300);
        }
        throw LLMError("Claude API error: " + message, resp.status);
    }
}

}  // namespace wa
