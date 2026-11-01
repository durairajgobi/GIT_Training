#include "ChatClient.h"

#include <stdexcept>
#include <utility>

using nlohmann::json;

namespace {
constexpr const char* kApiVersion = "2026-10-21";
// Server-side refusal fallback: if a safety classifier declines the request,
// the API retries it on an appropriate fallback model automatically.
constexpr const char* kFallbackBeta = "server-side-fallback-2026-07-01";
}  // namespace

ChatClient::ChatClient(ChatConfig config) : config_(std::move(config)) {
    if (config_.apiKey.empty()) {
        throw std::invalid_argument("ChatClient: API key must not be empty");
    }
}

void ChatClient::clearHistory() { history_ = json::array(); }

HttpClientHelper::Headers ChatClient::buildHeaders() const {
    return {
        {"content-type", "application/json"},
        {"x-api-key", config_.apiKey},
        {"anthropic-version", kApiVersion},
        {"anthropic-beta", kFallbackBeta},
    };
}

json ChatClient::buildRequest() const {
    return {
        {"model", config_.model},
        {"max_tokens", config_.maxTokens},
        {"system", config_.systemPrompt},
        {"thinking", {{"type", "adaptive"}}},
        {"output_config", {{"effort", config_.effort}}},
        {"fallbacks", "default"},
        {"messages", history_},
    };
}

std::string ChatClient::extractText(const json& content) {
    std::string text;
    for (const auto& block : content) {
        if (block.value("type", "") == "text") {
            text += block.value("text", "");
        }
    }
    return text;
}

std::string ChatClient::describeError(const HttpResponse& response) {
    std::string message = "API error (HTTP " + std::to_string(response.statusCode) + ")";
    json body = json::parse(response.body, nullptr, /*allow_exceptions=*/false);
    if (body.is_object() && body.contains("error")) {
        message += ": " + body["error"].value("type", "") + " - " +
                   body["error"].value("message", "");
    } else if (!response.body.empty()) {
        message += ": " + response.body;
    }
    return message;
}

std::string ChatClient::sendMessage(const std::string& userText) {
    history_.push_back({{"role", "user"}, {"content", userText}});

    HttpResponse response;
    try {
        response = http_.post(config_.endpoint, buildHeaders(), buildRequest().dump());
    } catch (...) {
        history_.erase(history_.end() - 1);  // keep history consistent
        throw;
    }

    if (!response.ok()) {
        history_.erase(history_.end() - 1);
        throw std::runtime_error(describeError(response));
    }

    json reply = json::parse(response.body);
    const std::string stopReason = reply.value("stop_reason", "");

    if (stopReason == "refusal") {
        history_.erase(history_.end() - 1);
        std::string reason = "The request was declined";
        if (reply.contains("stop_details") && reply["stop_details"].is_object()) {
            const auto& details = reply["stop_details"];
            if (details.contains("category") && details["category"].is_string()) {
                reason += " (" + details["category"].get<std::string>() + ")";
            }
        }
        return "[" + reason + ".]";
    }

    // Append the full content array (not just the text) so thinking blocks
    // are passed back unchanged on the next turn.
    history_.push_back({{"role", "assistant"}, {"content", reply["content"]}});

    std::string text = extractText(reply["content"]);
    if (stopReason == "max_tokens") {
        text += "\n[Response truncated: max_tokens reached.]";
    }
    return text;
}
