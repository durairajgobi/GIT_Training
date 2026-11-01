#pragma once
// Text generation via a local Ollama chat model (POST /api/chat).

#include <string>

#include "rag/http_client.hpp"

namespace rag {

class Llm {
public:
    Llm(std::string ollama_url, std::string model);

    std::string chat(const std::string& system_prompt, const std::string& user_prompt);

    const std::string& model() const { return model_; }

private:
    HttpClient http_;
    std::string url_;
    std::string model_;
};

}  // namespace rag
