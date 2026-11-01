#pragma once
// Minimal JSON-over-HTTP client built on libcurl.

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace rag {

using json = nlohmann::json;

struct HttpResponse {
    long status = 0;
    std::string body;
    bool ok() const { return status >= 200 && status < 300; }
};

class HttpError : public std::runtime_error {
public:
    HttpError(const std::string& what, long status)
        : std::runtime_error(what), status_(status) {}
    long status() const { return status_; }

private:
    long status_;
};

class HttpClient {
public:
    explicit HttpClient(long timeout_seconds = 300);
    ~HttpClient();
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    // Raw request. `method` is GET/POST/PUT/DELETE. Never throws on HTTP status.
    HttpResponse request(const std::string& method, const std::string& url,
                         const std::string& body = {});

    // JSON helpers: throw HttpError on non-2xx responses.
    json get_json(const std::string& url);
    json post_json(const std::string& url, const json& payload);
    json put_json(const std::string& url, const json& payload);

private:
    json send_json(const std::string& method, const std::string& url, const json& payload);
    void* curl_;  // CURL*
    long timeout_;
};

}  // namespace rag
