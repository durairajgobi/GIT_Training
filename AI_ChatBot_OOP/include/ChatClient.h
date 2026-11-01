#pragma once

#include "HttpClientHelper.h"

#include <nlohmann/json.hpp>

#include <string>

// Phase 2 - ChatClient: composition and a clean interface.
//
// ChatClient HAS-A HttpClientHelper (composition, not inheritance). It owns
// the conversation state and knows the Claude Messages API format; it knows
// nothing about libcurl. main() only talks to ChatClient.

struct ChatConfig {
    std::string apiKey;
    std::string model = "claude-opus-5-5";
    std::string systemPrompt = "You are a helpful, concise assistant.";
    std::string effort = "medium";  // low | medium | high | xhigh | max
    int maxTokens = 16000;
    std::string endpoint = "https://api.anthropic.com/v1/messages";
};

class ChatClient {
public:
    explicit ChatClient(ChatConfig config);

    // Sends a user message, records both turns in the history and returns
    // the assistant's text reply. Throws std::runtime_error on API errors.
    std::string sendMessage(const std::string& userText);

    void clearHistory();
    size_t turnCount() const { return history_.size(); }
    const ChatConfig& config() const { return config_; }

private:
    nlohmann::json buildRequest() const;
    HttpClientHelper::Headers buildHeaders() const;
    static std::string extractText(const nlohmann::json& content);
    static std::string describeError(const HttpResponse& response);

    ChatConfig config_;
    HttpClientHelper http_;          // composition
    nlohmann::json history_ = nlohmann::json::array();
};
