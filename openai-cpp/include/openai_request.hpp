// Building the OpenAI request (Responses API: POST /v1/responses)
#pragma once
#include <optional>
#include <string>
#include <nlohmann/json.hpp>

namespace openai {

struct ResponseRequest {
    std::string model = "gpt-4o-mini";       // cheap default; see notes/05
    std::string input;                         // the user's prompt
    std::optional<std::string> instructions;   // system-style guidance
    std::optional<int> max_output_tokens;      // hard cap on output = cost cap
    std::optional<double> temperature;         // 0.0 - 2.0
};

// Validates the request and converts it to the JSON body.
// Throws std::invalid_argument for bad input (before we spend money).
nlohmann::json build_request_body(const ResponseRequest& req);

}  // namespace openai
