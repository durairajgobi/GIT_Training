#pragma once
#include "llm_client.hpp"

namespace llm {

// Talks to any server that implements the OpenAI Chat Completions API:
// OpenAI itself, or Ollama at http://localhost:11434/v1.
class OpenAiClient : public LlmClient {
public:
    OpenAiClient(std::string baseUrl, std::string apiKey, std::string model);

    std::string chat(const std::vector<Message>& messages,
                     const ChatOptions& options = {}) override;
    std::vector<std::string> listModels() override;
    std::string description() const override;

private:
    std::vector<std::string> headers() const;

    std::string baseUrl_;
    std::string apiKey_;
    std::string model_;
};

}  // namespace llm
