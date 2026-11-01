#pragma once
// Provider-neutral interface. main.cpp only talks to this, which is what lets
// us swap OpenAI for Ollama (or anything else) without touching app code.
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace llm {

struct Message {
    std::string role;     // "system", "user" or "assistant"
    std::string content;
};

struct ChatOptions {
    double temperature = 0.7;
    std::optional<int> maxTokens;
};

using TokenHandler = std::function<void(const std::string& token)>;

class LlmError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class LlmClient {
public:
    virtual ~LlmClient() = default;

    // Send the whole conversation, get back the assistant's reply.
    virtual std::string chat(const std::vector<Message>& messages,
                             const ChatOptions& options = {}) = 0;

    // Same, but tokens are delivered to onToken as they are generated.
    // Default implementation: no real streaming, deliver the reply in one go.
    virtual std::string chatStream(const std::vector<Message>& messages,
                                   const TokenHandler& onToken,
                                   const ChatOptions& options = {}) {
        std::string reply = chat(messages, options);
        onToken(reply);
        return reply;
    }

    // Model names the server can serve (may be empty if unsupported).
    virtual std::vector<std::string> listModels() { return {}; }

    virtual std::string description() const = 0;
};

enum class Provider {
    OpenAI,        // api.openai.com, needs OPENAI_API_KEY
    OllamaNative,  // Ollama's own /api/chat endpoint (supports streaming)
    OllamaOpenAI,  // Ollama's OpenAI-compatible /v1 endpoint
};

struct ClientConfig {
    Provider provider = Provider::OllamaNative;
    std::string model;      // empty = provider default
    std::string baseUrl;    // empty = provider default
    std::string apiKey;     // only used by OpenAI
};

Provider parseProvider(const std::string& name);
std::unique_ptr<LlmClient> makeClient(const ClientConfig& config);

}  // namespace llm
