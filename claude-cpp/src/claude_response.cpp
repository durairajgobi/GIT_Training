#include "claude_response.hpp"

#include <nlohmann/json.hpp>

namespace claude {

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

// Steps 1-2 shared by both endpoints: valid JSON? HTTP error?
json parse_and_check(long http_status, const std::string& body) {
    json j = json::parse(body, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object())
        throw ApiError(http_status, "invalid_response",
                       "Response body is not a JSON object (HTTP " +
                           std::to_string(http_status) + ")");

    // Errors look like {"type":"error","error":{"type":"...","message":"..."}}
    if (http_status < 200 || http_status >= 300 || str_or(j, "type") == "error") {
        const json err = j.value("error", json::object());
        throw ApiError(http_status, str_or(err, "type", "http_error"),
                       str_or(err, "message", "HTTP " + std::to_string(http_status)));
    }
    return j;
}

}  // namespace

ParsedResponse parse_response(long http_status, const std::string& body) {
    const json j = parse_and_check(http_status, body);

    ParsedResponse out;
    out.id = str_or(j, "id");
    out.model = str_or(j, "model");
    out.stop_reason = str_or(j, "stop_reason");

    // 3. Usage first, so a caller can still log cost on a refusal.
    if (j.contains("usage") && j["usage"].is_object()) {
        const json& u = j["usage"];
        out.usage.input_tokens = long_or(u, "input_tokens");
        out.usage.cache_creation_input_tokens = long_or(u, "cache_creation_input_tokens");
        out.usage.cache_read_input_tokens = long_or(u, "cache_read_input_tokens");
        out.usage.output_tokens = long_or(u, "output_tokens");
        // A "fallback_message" iteration means a fallback model answered.
        if (u.contains("iterations") && u["iterations"].is_array())
            for (const auto& it : u["iterations"])
                if (str_or(it, "type") == "fallback_message") out.served_by_fallback = true;
    }

    // 4. Refusal is HTTP 200 - always check stop_reason before reading content.
    if (out.stop_reason == "refusal") {
        std::string msg = "Claude declined the request";
        if (j.contains("stop_details") && j["stop_details"].is_object()) {
            const std::string cat = str_or(j["stop_details"], "category");
            if (!cat.empty()) msg += " (category: " + cat + ")";
        }
        throw ApiError(http_status, "refusal", msg);
    }

    // 5. Collect text. content[] can also hold "thinking" and "fallback"
    //    blocks - skip anything that is not "text".
    if (j.contains("content") && j["content"].is_array()) {
        for (const auto& block : j["content"]) {
            if (str_or(block, "type") != "text") continue;
            if (!out.text.empty()) out.text += "\n";
            out.text += str_or(block, "text");
        }
    }

    // 6. Empty answer with no explanation is an error; a max_tokens
    //    cut-off is returned so the caller can decide what to do.
    if (out.text.empty() && !out.truncated())
        throw ApiError(http_status, "empty_output",
                       "Response contained no text (stop_reason: " + out.stop_reason + ")");
    return out;
}

long parse_count_tokens(long http_status, const std::string& body) {
    const json j = parse_and_check(http_status, body);
    if (!j.contains("input_tokens") || !j["input_tokens"].is_number_integer())
        throw ApiError(http_status, "invalid_response", "count_tokens: missing input_tokens");
    return j["input_tokens"].get<long>();
}

}  // namespace claude
