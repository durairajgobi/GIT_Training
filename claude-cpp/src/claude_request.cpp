#include "claude_request.hpp"

#include <set>
#include <stdexcept>

namespace claude {

using nlohmann::json;

namespace {

void validate(const MessageRequest& req) {
    if (req.model.empty())
        throw std::invalid_argument("model must not be empty");
    if (req.messages.empty())
        throw std::invalid_argument("messages must not be empty");
    for (const auto& m : req.messages) {
        if (m.role != "user" && m.role != "assistant")
            throw std::invalid_argument("role must be 'user' or 'assistant'");
        if (m.content.empty())
            throw std::invalid_argument("message content must not be empty");
    }
    // Current models reject assistant prefill: the last turn must be the user's.
    if (req.messages.back().role != "user")
        throw std::invalid_argument("the last message must have role 'user'");
    static const std::set<std::string> efforts = {"low", "medium", "high", "xhigh", "max"};
    if (req.effort && !efforts.count(*req.effort))
        throw std::invalid_argument("effort must be low|medium|high|xhigh|max");
}

json messages_json(const MessageRequest& req) {
    json arr = json::array();
    for (const auto& m : req.messages) arr.push_back({{"role", m.role}, {"content", m.content}});
    return arr;
}

}  // namespace

json build_request_body(const MessageRequest& req) {
    validate(req);
    if (req.max_tokens < 1)
        throw std::invalid_argument("max_tokens must be >= 1");

    // nlohmann::json escapes quotes, newlines, unicode etc. for us.
    json body = {
        {"model", req.model},
        {"max_tokens", req.max_tokens},
        {"messages", messages_json(req)},
    };
    if (req.system) body["system"] = *req.system;
    if (req.effort) body["output_config"] = {{"effort", *req.effort}};
    if (req.fallbacks_default) body["fallbacks"] = "default";
    // No "thinking" field: Claude Opus 5.5 always uses adaptive thinking.
    // No temperature/top_p/top_k: API version 2026-10-21 rejects them.
    return body;
}

json build_count_tokens_body(const MessageRequest& req) {
    validate(req);
    json body = {{"model", req.model}, {"messages", messages_json(req)}};
    if (req.system) body["system"] = *req.system;
    return body;
}

std::vector<std::string> request_headers(const MessageRequest& req) {
    std::vector<std::string> h;
    if (req.fallbacks_default) h.push_back("anthropic-beta: server-side-fallback-2026-07-01");
    return h;
}

}  // namespace claude
