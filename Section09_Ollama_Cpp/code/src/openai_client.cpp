#include "openai_client.hpp"

#include <nlohmann/json.hpp>

#include "http.hpp"

using nlohmann::json;

namespace llm {

OpenAiClient::OpenAiClient(std::string baseUrl, std::string apiKey,
                           std::string model)
    : baseUrl_(std::move(baseUrl)),
      apiKey_(std::move(apiKey)),
      model_(std::move(model)) {}

std::vector<std::string> OpenAiClient::headers() const {
    std::vector<std::string> h{"Content-Type: application/json"};
    // Ollama ignores the key, but the header must still be well-formed.
    if (!apiKey_.empty()) h.push_back("Authorization: Bearer " + apiKey_);
    return h;
}

std::string OpenAiClient::chat(const std::vector<Message>& messages,
                               const ChatOptions& options) {
    json request{{"model", model_},
                 {"temperature", options.temperature},
                 {"messages", json::array()}};
    for (const auto& m : messages)
        request["messages"].push_back({{"role", m.role}, {"content", m.content}});
    if (options.maxTokens) request["max_tokens"] = *options.maxTokens;

    auto res = http::post(baseUrl_ + "/chat/completions", request.dump(),
                          headers());
    json body = json::parse(res.body, nullptr, /*allow_exceptions=*/false);

    if (res.status != 200 || body.is_discarded()) {
        std::string msg = body.is_discarded()
                              ? res.body
                              : body.value("/error/message"_json_pointer,
                                           res.body);
        throw LlmError("OpenAI-compatible API returned HTTP " +
                       std::to_string(res.status) + ": " + msg);
    }
    return body.at("choices").at(0).at("message").at("content").get<std::string>();
}

std::vector<std::string> OpenAiClient::listModels() {
    auto res = http::get(baseUrl_ + "/models", headers());
    if (res.status != 200)
        throw LlmError("GET /models returned HTTP " + std::to_string(res.status));
    std::vector<std::string> names;
    // Keep the parsed json alive: looping over a member of a temporary
    // would dangle (the temporary dies before the loop body runs).
    const json body = json::parse(res.body);
    for (const auto& m : body.at("data"))
        names.push_back(m.at("id").get<std::string>());
    return names;
}

std::string OpenAiClient::description() const {
    return "OpenAI-compatible API at " + baseUrl_ + " (model: " + model_ + ")";
}

}  // namespace llm
