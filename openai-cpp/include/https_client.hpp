// Making the HTTPS call to OpenAI with libcurl
#pragma once
#include <string>

namespace openai {

struct HttpResult {
    long status_code = 0;  // HTTP status, e.g. 200, 401, 429
    std::string body;      // raw response body
};

struct HttpOptions {
    std::string base_url = "https://api.openai.com/v1";
    long timeout_seconds = 60;
    long connect_timeout_seconds = 10;
};

// POSTs a JSON body to base_url + path with "Authorization: Bearer <key>".
// Throws std::runtime_error on transport failures (DNS, TLS, timeout...).
// HTTP error codes (4xx/5xx) are NOT thrown here; they are returned so the
// caller can parse OpenAI's error JSON.
HttpResult post_json(const std::string& path,
                     const std::string& json_body,
                     const std::string& api_key,
                     const HttpOptions& opts = {});

// Call once at program start / end (wraps curl_global_init/cleanup).
struct CurlGlobal {
    CurlGlobal();
    ~CurlGlobal();
    CurlGlobal(const CurlGlobal&) = delete;
    CurlGlobal& operator=(const CurlGlobal&) = delete;
};

}  // namespace openai
