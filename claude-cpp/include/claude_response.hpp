// Parsing & validating the Claude response
#pragma once
#include <stdexcept>
#include <string>

namespace claude {

struct Usage {
    long input_tokens = 0;                 // uncached input only
    long cache_creation_input_tokens = 0;  // written to the prompt cache
    long cache_read_input_tokens = 0;      // served from the prompt cache
    long output_tokens = 0;                // includes thinking tokens
    long total_input_tokens() const {
        return input_tokens + cache_creation_input_tokens + cache_read_input_tokens;
    }
};

struct ParsedResponse {
    std::string id;
    std::string model;          // the model that actually produced the answer
    std::string stop_reason;    // "end_turn", "max_tokens", "stop_sequence", ...
    std::string text;           // all "text" blocks joined
    bool served_by_fallback = false;
    Usage usage;
    bool truncated() const { return stop_reason == "max_tokens"; }
};

// Thrown when the API returned an error or the payload is not usable.
class ApiError : public std::runtime_error {
public:
    ApiError(long http_status, std::string type, const std::string& message)
        : std::runtime_error(message), http_status(http_status), type(std::move(type)) {}
    long http_status;
    std::string type;  // e.g. "rate_limit_error", "overloaded_error", "refusal"
    // 408/409/429 and 5xx (including 529 overloaded) are worth retrying
    // with exponential backoff.
    bool retryable() const {
        return http_status == 408 || http_status == 409 || http_status == 429 ||
               http_status >= 500;
    }
};

// Parses a /v1/messages result. Throws ApiError for HTTP errors, invalid
// JSON, refusals, or responses with no text and no explanation.
ParsedResponse parse_response(long http_status, const std::string& body);

// Parses a /v1/messages/count_tokens result -> input_tokens.
long parse_count_tokens(long http_status, const std::string& body);

}  // namespace claude
