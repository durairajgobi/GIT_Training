#include "rag/llm.hpp"

#include <stdexcept>

namespace rag {

Llm::Llm(std::string ollama_url, std::string model)
    : url_(std::move(ollama_url)), model_(std::move(model)) {}

std::string Llm::chat(const std::string& system_prompt, const std::string& user_prompt) {
    const json req = {
        {"model", model_},
        {"stream", false},
        {"options", {{"temperature", 0.1}}},
        {"messages",
         json::array({{{"role", "system"}, {"content", system_prompt}},
                      {{"role", "user"}, {"content", user_prompt}}})},
    };
    const json resp = http_.post_json(url_ + "/api/chat", req);
    if (!resp.contains("message") || !resp["message"].contains("content"))
        throw std::runtime_error("Unexpected Ollama /api/chat response: " + resp.dump());
    return resp["message"]["content"].get<std::string>();
}

}  // namespace rag
