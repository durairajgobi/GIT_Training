#pragma once
#include "llm_client.hpp"

namespace llm {

// Talks to Ollama's native REST API (/api/chat, /api/tags).
class OllamaClient : public LlmClient {
public:
    OllamaClient(std::string host, std::string model);

    std::string chat(const std::vector<Message>& messages,
                     const ChatOptions& options = {}) override;
    std::string chatStream(const std::vector<Message>& messages,
                           const TokenHandler& onToken,
                           const ChatOptions& options = {}) override;
    std::vector<std::string> listModels() override;
    std::string description() const override;

private:
    std::string buildRequest(const std::vector<Message>& messages,
                             const ChatOptions& options, bool stream) const;

    std::string host_;
    std::string model_;
};

}  // namespace llm
