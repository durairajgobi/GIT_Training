// Building the Claude request (Messages API: POST /v1/messages)
#pragma once
#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace claude {

struct Message {
    std::string role;     // "user" or "assistant"
    std::string content;  // plain text
};

struct MessageRequest {
    std::string model = "claude-opus-5-5";
    int max_tokens = 16000;                // required; hard cap on output tokens
    std::optional<std::string> system;     // system prompt
    std::vector<Message> messages;         // conversation, must end with a user turn
    // "low" | "medium" | "high" | "xhigh" | "max". Controls thinking depth
    // and token spend. Claude Opus 5.5 defaults to "medium" when unset.
    std::optional<std::string> effort;
    // Server-side fallback: if a safety classifier declines the request, the
    // API retries it on Anthropic's recommended model for that category.
    bool fallbacks_default = true;
};

// Validates the request and converts it to the JSON body.
// Throws std::invalid_argument for bad input (before we spend money).
nlohmann::json build_request_body(const MessageRequest& req);

// Body for POST /v1/messages/count_tokens (free pre-flight token count).
nlohmann::json build_count_tokens_body(const MessageRequest& req);

// Extra headers this request needs (e.g. anthropic-beta).
std::vector<std::string> request_headers(const MessageRequest& req);

}  // namespace claude
