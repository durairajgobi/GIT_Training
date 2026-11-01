#include "ollama_client.hpp"

#include <nlohmann/json.hpp>

#include "http.hpp"

using nlohmann::json;

namespace llm {
namespace {
const std::vector<std::string> kJsonHeaders{"Content-Type: application/json"};

[[noreturn]] void throwHttpError(long status, const std::string& body) {
    json j = json::parse(body, nullptr, false);
    std::string msg = (!j.is_discarded() && j.contains("error"))
                          ? j["error"].get<std::string>()
                          : body;
    throw LlmError("Ollama returned HTTP " + std::to_string(status) + ": " + msg);
}
}  // namespace

OllamaClient::OllamaClient(std::string host, std::string model)
    : host_(std::move(host)), model_(std::move(model)) {}

std::string OllamaClient::buildRequest(const std::vector<Message>& messages,
                                       const ChatOptions& options,
                                       bool stream) const {
    json request{{"model", model_},
                 {"stream", stream},
                 {"messages", json::array()},
                 {"options", {{"temperature", options.temperature}}}};
    for (const auto& m : messages)
        request["messages"].push_back({{"role", m.role}, {"content", m.content}});
    // Ollama calls max tokens "num_predict".
    if (options.maxTokens) request["options"]["num_predict"] = *options.maxTokens;
    return request.dump();
}

std::string OllamaClient::chat(const std::vector<Message>& messages,
                               const ChatOptions& options) {
    auto res = http::post(host_ + "/api/chat",
                          buildRequest(messages, options, false), kJsonHeaders);
    if (res.status != 200) throwHttpError(res.status, res.body);
    return json::parse(res.body).at("message").at("content").get<std::string>();
}

std::string OllamaClient::chatStream(const std::vector<Message>& messages,
                                     const TokenHandler& onToken,
                                     const ChatOptions& options) {
    // Ollama streams newline-delimited JSON (NDJSON): one object per line.
    // Network chunks don't line up with lines, so buffer until we see '\n'.
    std::string pending, reply, errorBody;
    auto handleLine = [&](const std::string& line) {
        if (line.empty()) return;
        json j = json::parse(line, nullptr, false);
        if (j.is_discarded() || j.contains("error")) {
            errorBody += line;
            return;
        }
        if (j.contains("message")) {
            std::string token = j["message"].value("content", "");
            reply += token;
            if (!token.empty()) onToken(token);
        }
    };

    auto res = http::post(
        host_ + "/api/chat", buildRequest(messages, options, true), kJsonHeaders,
        [&](const char* data, size_t len) {
            pending.append(data, len);
            size_t pos;
            while ((pos = pending.find('\n')) != std::string::npos) {
                handleLine(pending.substr(0, pos));
                pending.erase(0, pos + 1);
            }
        });
    handleLine(pending);  // last line may lack a trailing newline

    if (res.status != 200) throwHttpError(res.status, errorBody);
    if (!errorBody.empty()) throwHttpError(res.status, errorBody);
    return reply;
}

std::vector<std::string> OllamaClient::listModels() {
    auto res = http::get(host_ + "/api/tags");
    if (res.status != 200) throwHttpError(res.status, res.body);
    std::vector<std::string> names;
    // Keep the parsed json alive: looping over a member of a temporary
    // would dangle (the temporary dies before the loop body runs).
    const json body = json::parse(res.body);
    for (const auto& m : body.at("models"))
        names.push_back(m.at("name").get<std::string>());
    return names;
}

std::string OllamaClient::description() const {
    return "Ollama native API at " + host_ + " (model: " + model_ + ")";
}

}  // namespace llm
