// Making the HTTPS call to the Claude API with libcurl
#pragma once
#include <string>
#include <vector>

namespace claude {

// API version header sent on every request.
inline constexpr const char* kAnthropicVersion = "2026-10-21";

struct HttpResult {
    long status_code = 0;    // HTTP status, e.g. 200, 401, 429, 529
    std::string body;        // raw response body
    std::string request_id;  // "request-id" response header (quote it in support tickets)
};

struct HttpOptions {
    std::string base_url = "https://api.anthropic.com/v1";
    long timeout_seconds = 600;  // long answers can take minutes
    long connect_timeout_seconds = 10;
};

// POSTs JSON to base_url + path with the x-api-key and anthropic-version
// headers plus any extra_headers. Throws std::runtime_error on transport
// failures (DNS, TLS, timeout...). HTTP 4xx/5xx are returned, not thrown.
HttpResult post_json(const std::string& path,
                     const std::string& json_body,
                     const std::string& api_key,
                     const std::vector<std::string>& extra_headers = {},
                     const HttpOptions& opts = {});

// Call once at program start / end (wraps curl_global_init/cleanup).
struct CurlGlobal {
    CurlGlobal();
    ~CurlGlobal();
    CurlGlobal(const CurlGlobal&) = delete;
    CurlGlobal& operator=(const CurlGlobal&) = delete;
};

}  // namespace claude
