#include "openai_request.hpp"

#include <stdexcept>

namespace openai {

nlohmann::json build_request_body(const ResponseRequest& req) {
    if (req.model.empty())
        throw std::invalid_argument("model must not be empty");
    if (req.input.empty())
        throw std::invalid_argument("input must not be empty");
    if (req.max_output_tokens && *req.max_output_tokens < 16)
        throw std::invalid_argument("max_output_tokens must be >= 16");
    if (req.temperature && (*req.temperature < 0.0 || *req.temperature > 2.0))
        throw std::invalid_argument("temperature must be between 0 and 2");

    // nlohmann::json escapes quotes, newlines, unicode etc. for us -
    // never build JSON by string concatenation.
    nlohmann::json body = {
        {"model", req.model},
        {"input", req.input},
    };
    if (req.instructions) body["instructions"] = *req.instructions;
    if (req.max_output_tokens) body["max_output_tokens"] = *req.max_output_tokens;
    if (req.temperature) body["temperature"] = *req.temperature;
    return body;
}

}  // namespace openai
