#include "https_client.hpp"

#include <curl/curl.h>

#include <memory>
#include <stdexcept>

namespace openai {

CurlGlobal::CurlGlobal() {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        throw std::runtime_error("curl_global_init failed");
}
CurlGlobal::~CurlGlobal() { curl_global_cleanup(); }

namespace {

// libcurl calls this for every chunk of the response body.
size_t write_callback(char* data, size_t size, size_t nmemb, void* userp) {
    auto* out = static_cast<std::string*>(userp);
    out->append(data, size * nmemb);
    return size * nmemb;
}

// RAII wrappers so handles are freed even if we throw.
struct CurlDeleter { void operator()(CURL* c) const { curl_easy_cleanup(c); } };
struct SlistDeleter { void operator()(curl_slist* s) const { curl_slist_free_all(s); } };

}  // namespace

HttpResult post_json(const std::string& path, const std::string& json_body,
                     const std::string& api_key, const HttpOptions& opts) {
    if (api_key.empty())
        throw std::runtime_error("API key is empty (set OPENAI_API_KEY)");

    std::unique_ptr<CURL, CurlDeleter> curl(curl_easy_init());
    if (!curl) throw std::runtime_error("curl_easy_init failed");

    const std::string url = opts.base_url + path;
    const std::string auth = "Authorization: Bearer " + api_key;

    curl_slist* raw = nullptr;
    raw = curl_slist_append(raw, "Content-Type: application/json");
    raw = curl_slist_append(raw, auth.c_str());
    std::unique_ptr<curl_slist, SlistDeleter> headers(raw);

    HttpResult result;
    char errbuf[CURL_ERROR_SIZE] = {0};

    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, json_body.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(json_body.size()));
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &result.body);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, opts.timeout_seconds);
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, opts.connect_timeout_seconds);
    curl_easy_setopt(curl.get(), CURLOPT_ERRORBUFFER, errbuf);
    // TLS verification is ON by default - never turn it off.
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);

    const CURLcode rc = curl_easy_perform(curl.get());
    if (rc != CURLE_OK) {
        std::string msg = "HTTPS request failed: ";
        msg += errbuf[0] ? errbuf : curl_easy_strerror(rc);
        throw std::runtime_error(msg);
    }
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &result.status_code);
    return result;
}

}  // namespace openai
