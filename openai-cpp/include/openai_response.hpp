// Parsing & validating the OpenAI response
#pragma once
#include <stdexcept>
#include <string>

namespace openai {

struct Usage {
    long input_tokens = 0;
    long output_tokens = 0;
    long total_tokens = 0;
    long cached_input_tokens = 0;
    long reasoning_tokens = 0;
};

struct ParsedResponse {
    std::string id;
    std::string model;
    std::string status;            // "completed", "incomplete", ...
    std::string text;              // all output_text parts joined
    std::string incomplete_reason; // e.g. "max_output_tokens"
    Usage usage;
};

// Thrown when the API returned an error or the payload is not usable.
class ApiError : public std::runtime_error {
public:
    ApiError(long http_status, std::string type, std::string code,
             const std::string& message)
        : std::runtime_error(message),
          http_status(http_status), type(std::move(type)), code(std::move(code)) {}
    long http_status;
    std::string type;
    std::string code;
    // 429 (rate limit) and 5xx are usually worth retrying with backoff.
    bool retryable() const { return http_status == 429 || http_status >= 500; }
};

// Parses the raw HTTP result. Throws ApiError for HTTP errors, invalid JSON,
// failed responses, or responses with no text output.
ParsedResponse parse_response(long http_status, const std::string& body);

}  // namespace openai
