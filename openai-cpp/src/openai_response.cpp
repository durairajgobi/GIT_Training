#include "openai_response.hpp"

#include <nlohmann/json.hpp>

namespace openai {

using nlohmann::json;

namespace {

std::string str_or(const json& j, const char* key, std::string fallback = "") {
    auto it = j.find(key);
    return (it != j.end() && it->is_string()) ? it->get<std::string>() : fallback;
}

long long_or(const json& j, const char* key) {
    auto it = j.find(key);
    return (it != j.end() && it->is_number_integer()) ? it->get<long>() : 0;
}

}  // namespace

ParsedResponse parse_response(long http_status, const std::string& body) {
    // 1. Is it JSON at all? (proxies / gateways may return HTML)
    json j = json::parse(body, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object())
        throw ApiError(http_status, "invalid_response", "",
                       "Response body is not a JSON object (HTTP " +
                           std::to_string(http_status) + ")");

    // 2. HTTP-level error: OpenAI sends {"error": {"message","type","code"}}
    if (http_status < 200 || http_status >= 300) {
        const json err = j.value("error", json::object());
        std::string code = err.contains("code") && err["code"].is_string()
                               ? err["code"].get<std::string>() : "";
        throw ApiError(http_status, str_or(err, "type", "http_error"), code,
                       str_or(err, "message", "HTTP " + std::to_string(http_status)));
    }

    // 3. A 200 can still carry a failed response.
    if (j.contains("error") && j["error"].is_object())
        throw ApiError(http_status, str_or(j["error"], "type", "response_error"),
                       str_or(j["error"], "code"),
                       str_or(j["error"], "message", "Response reported an error"));

    ParsedResponse out;
    out.id = str_or(j, "id");
    out.model = str_or(j, "model");
    out.status = str_or(j, "status");

    if (out.status == "failed" || out.status == "cancelled")
        throw ApiError(http_status, "response_" + out.status, "",
                       "Response status is '" + out.status + "'");

    if (j.contains("incomplete_details") && j["incomplete_details"].is_object())
        out.incomplete_reason = str_or(j["incomplete_details"], "reason");

    // 4. Collect text: output[] -> {type:"message"} -> content[] -> {type:"output_text"}
    if (j.contains("output") && j["output"].is_array()) {
        for (const auto& item : j["output"]) {
            if (str_or(item, "type") != "message" || !item.contains("content")) continue;
            for (const auto& part : item["content"]) {
                const std::string type = str_or(part, "type");
                if (type == "output_text") {
                    if (!out.text.empty()) out.text += "\n";
                    out.text += str_or(part, "text");
                } else if (type == "refusal") {
                    throw ApiError(http_status, "refusal", "",
                                   "Model refused: " + str_or(part, "refusal"));
                }
            }
        }
    }

    // 5. Token usage (needed for cost tracking)
    if (j.contains("usage") && j["usage"].is_object()) {
        const json& u = j["usage"];
        out.usage.input_tokens = long_or(u, "input_tokens");
        out.usage.output_tokens = long_or(u, "output_tokens");
        out.usage.total_tokens = long_or(u, "total_tokens");
        if (u.contains("input_tokens_details") && u["input_tokens_details"].is_object())
            out.usage.cached_input_tokens = long_or(u["input_tokens_details"], "cached_tokens");
        if (u.contains("output_tokens_details") && u["output_tokens_details"].is_object())
            out.usage.reasoning_tokens = long_or(u["output_tokens_details"], "reasoning_tokens");
    }

    if (out.text.empty() && out.incomplete_reason.empty())
        throw ApiError(http_status, "empty_output", "", "Response contained no output_text");

    return out;
}

}  // namespace openai
