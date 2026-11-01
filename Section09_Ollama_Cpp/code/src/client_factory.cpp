#include <algorithm>
#include <cctype>

#include "llm_client.hpp"
#include "ollama_client.hpp"
#include "openai_client.hpp"

namespace llm {

Provider parseProvider(const std::string& name) {
    std::string n = name;
    std::transform(n.begin(), n.end(), n.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (n == "openai") return Provider::OpenAI;
    if (n == "ollama") return Provider::OllamaNative;
    if (n == "ollama-openai") return Provider::OllamaOpenAI;
    throw std::invalid_argument("Unknown provider '" + name +
                                "' (use openai, ollama or ollama-openai)");
}

std::unique_ptr<LlmClient> makeClient(const ClientConfig& c) {
    auto orDefault = [](const std::string& v, const char* def) {
        return v.empty() ? std::string(def) : v;
    };
    switch (c.provider) {
        case Provider::OpenAI:
            if (c.apiKey.empty())
                throw std::invalid_argument("OPENAI_API_KEY is not set");
            return std::make_unique<OpenAiClient>(
                orDefault(c.baseUrl, "https://api.openai.com/v1"), c.apiKey,
                orDefault(c.model, "gpt-4o-mini"));
        case Provider::OllamaNative:
            return std::make_unique<OllamaClient>(
                orDefault(c.baseUrl, "http://localhost:11434"),
                orDefault(c.model, "llama3.2"));
        case Provider::OllamaOpenAI:
            // The only changes vs. OpenAI: base URL, model name, dummy key.
            return std::make_unique<OpenAiClient>(
                orDefault(c.baseUrl, "http://localhost:11434") + "/v1",
                orDefault(c.apiKey, "ollama"), orDefault(c.model, "llama3.2"));
    }
    throw std::logic_error("unreachable");
}

}  // namespace llm
