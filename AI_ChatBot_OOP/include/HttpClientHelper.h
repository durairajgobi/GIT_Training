#pragma once

#include <map>
#include <string>

// Phase 1 - HttpClientHelper: encapsulates the HTTP layer (libcurl).
//
// Everything libcurl-specific lives behind this class: global init/cleanup,
// the CURL* handle (RAII), header lists, the write callback and error codes.
// Callers only ever see std::string in, HttpResponse out.

struct HttpResponse {
    long statusCode = 0;
    std::string body;

    bool ok() const { return statusCode >= 200 && statusCode < 300; }
};

class HttpClientHelper {
public:
    using Headers = std::map<std::string, std::string>;

    explicit HttpClientHelper(long timeoutSeconds = 600);
    ~HttpClientHelper();

    // The CURL handle is a unique resource: no copies, moves are fine.
    HttpClientHelper(const HttpClientHelper&) = delete;
    HttpClientHelper& operator=(const HttpClientHelper&) = delete;
    HttpClientHelper(HttpClientHelper&& other) noexcept;
    HttpClientHelper& operator=(HttpClientHelper&& other) noexcept;

    // Sends a POST request. Throws std::runtime_error on transport failures
    // (DNS, TLS, timeout...). HTTP error statuses are returned, not thrown.
    HttpResponse post(const std::string& url,
                      const Headers& headers,
                      const std::string& body) const;

private:
    static size_t writeCallback(char* data, size_t size, size_t nmemb, void* userp);

    void* curl_ = nullptr;   // CURL* (kept opaque so the header needs no curl include)
    long timeoutSeconds_;
};
