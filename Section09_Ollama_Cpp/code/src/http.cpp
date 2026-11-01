#include "http.hpp"

#include <curl/curl.h>

#include <memory>
#include <stdexcept>

namespace http {
namespace {

struct WriteContext {
    std::string* body;
    const ChunkHandler* onChunk;
};

size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* ctx = static_cast<WriteContext*>(userdata);
    const size_t len = size * nmemb;
    if (ctx->onChunk && *ctx->onChunk) {
        (*ctx->onChunk)(ptr, len);
    } else {
        ctx->body->append(ptr, len);
    }
    return len;
}

using CurlPtr = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
using HeaderPtr = std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>;

Response perform(const std::string& url, const std::string* postBody,
                 const std::vector<std::string>& headers,
                 const ChunkHandler* onChunk, long timeoutSeconds) {
    CurlPtr curl(curl_easy_init(), &curl_easy_cleanup);
    if (!curl) throw std::runtime_error("curl_easy_init failed");

    HeaderPtr headerList(nullptr, &curl_slist_free_all);
    for (const auto& h : headers) {
        curl_slist* next = curl_slist_append(headerList.get(), h.c_str());
        if (!next) throw std::runtime_error("curl_slist_append failed");
        headerList.release();
        headerList.reset(next);
    }

    Response response;
    WriteContext ctx{&response.body, onChunk};

    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headerList.get());
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, timeoutSeconds);
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 10L);
    if (postBody) {
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, postBody->c_str());
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE_LARGE,
                         static_cast<curl_off_t>(postBody->size()));
    }

    CURLcode rc = curl_easy_perform(curl.get());
    if (rc != CURLE_OK) {
        throw std::runtime_error(std::string("HTTP request to ") + url +
                                 " failed: " + curl_easy_strerror(rc));
    }
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &response.status);
    return response;
}

}  // namespace

GlobalInit::GlobalInit() {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        throw std::runtime_error("curl_global_init failed");
}
GlobalInit::~GlobalInit() { curl_global_cleanup(); }

Response get(const std::string& url, const std::vector<std::string>& headers,
             long timeoutSeconds) {
    return perform(url, nullptr, headers, nullptr, timeoutSeconds);
}

Response post(const std::string& url, const std::string& body,
              const std::vector<std::string>& headers,
              const ChunkHandler& onChunk, long timeoutSeconds) {
    return perform(url, &body, headers, onChunk ? &onChunk : nullptr,
                   timeoutSeconds);
}

}  // namespace http
