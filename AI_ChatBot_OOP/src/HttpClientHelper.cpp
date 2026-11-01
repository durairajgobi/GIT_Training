#include "HttpClientHelper.h"

#include <curl/curl.h>

#include <stdexcept>
#include <utility>

namespace {

// curl_global_init/cleanup must run exactly once per process.
// A function-local static gives us thread-safe, lazy, one-time init.
struct CurlGlobal {
    CurlGlobal() {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
            throw std::runtime_error("curl_global_init failed");
        }
    }
    ~CurlGlobal() { curl_global_cleanup(); }
};

void ensureCurlGlobal() {
    static CurlGlobal instance;
}

// RAII wrapper for curl_slist so headers are freed on every path.
struct HeaderList {
    curl_slist* list = nullptr;
    ~HeaderList() { curl_slist_free_all(list); }
    void add(const std::string& line) {
        curl_slist* next = curl_slist_append(list, line.c_str());
        if (!next) throw std::runtime_error("curl_slist_append failed");
        list = next;
    }
};

}  // namespace

HttpClientHelper::HttpClientHelper(long timeoutSeconds)
    : timeoutSeconds_(timeoutSeconds) {
    ensureCurlGlobal();
    curl_ = curl_easy_init();
    if (!curl_) throw std::runtime_error("curl_easy_init failed");
}

HttpClientHelper::~HttpClientHelper() {
    if (curl_) curl_easy_cleanup(static_cast<CURL*>(curl_));
}

HttpClientHelper::HttpClientHelper(HttpClientHelper&& other) noexcept
    : curl_(std::exchange(other.curl_, nullptr)),
      timeoutSeconds_(other.timeoutSeconds_) {}

HttpClientHelper& HttpClientHelper::operator=(HttpClientHelper&& other) noexcept {
    if (this != &other) {
        if (curl_) curl_easy_cleanup(static_cast<CURL*>(curl_));
        curl_ = std::exchange(other.curl_, nullptr);
        timeoutSeconds_ = other.timeoutSeconds_;
    }
    return *this;
}

size_t HttpClientHelper::writeCallback(char* data, size_t size, size_t nmemb, void* userp) {
    auto* out = static_cast<std::string*>(userp);
    out->append(data, size * nmemb);
    return size * nmemb;
}

HttpResponse HttpClientHelper::post(const std::string& url,
                                    const Headers& headers,
                                    const std::string& body) const {
    if (!curl_) throw std::logic_error("HttpClientHelper used after move");
    CURL* curl = static_cast<CURL*>(curl_);
    curl_easy_reset(curl);  // reuse the handle (keeps connection cache) with clean options

    HeaderList headerList;
    for (const auto& [name, value] : headers) {
        headerList.add(name + ": " + value);
    }

    HttpResponse response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList.list);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &HttpClientHelper::writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSeconds_);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    CURLcode rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) {
        throw std::runtime_error(std::string("HTTP request failed: ") + curl_easy_strerror(rc));
    }
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.statusCode);
    return response;
}
