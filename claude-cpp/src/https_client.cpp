#include "https_client.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <stdexcept>

namespace claude {

CurlGlobal::CurlGlobal() {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        throw std::runtime_error("curl_global_init failed");
}
CurlGlobal::~CurlGlobal() { curl_global_cleanup(); }

namespace {

// libcurl calls this for every chunk of the response body.
size_t write_callback(char* data, size_t size, size_t nmemb, void* userp) {
    static_cast<std::string*>(userp)->append(data, size * nmemb);
    return size * nmemb;
}

// libcurl calls this once per response header line; we keep "request-id".
size_t header_callback(char* data, size_t size, size_t nitems, void* userp) {
    std::string line(data, size * nitems);
    std::string lower = line;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const std::string key = "request-id:";
    if (lower.rfind(key, 0) == 0) {
        std::string value = line.substr(key.size());
        const auto b = value.find_first_not_of(" \t");
        const auto e = value.find_last_not_of(" \t\r\n");
        *static_cast<std::string*>(userp) = (b == std::string::npos) ? "" : value.substr(b, e - b + 1);
    }
    return size * nitems;
}

struct CurlDeleter { void operator()(CURL* c) const { curl_easy_cleanup(c); } };
struct SlistDeleter { void operator()(curl_slist* s) const { curl_slist_free_all(s); } };

}  // namespace

HttpResult post_json(const std::string& path, const std::string& json_body,
                     const std::string& api_key,
                     const std::vector<std::string>& extra_headers,
                     const HttpOptions& opts) {
    if (api_key.empty())
        throw std::runtime_error("API key is empty (set ANTHROPIC_API_KEY)");

    std::unique_ptr<CURL, CurlDeleter> curl(curl_easy_init());
    if (!curl) throw std::runtime_error("curl_easy_init failed");

    const std::string url = opts.base_url + path;
    const std::string key_header = "x-api-key: " + api_key;  // Claude uses x-api-key, not Bearer
    const std::string version_header = std::string("anthropic-version: ") + kAnthropicVersion;

    curl_slist* raw = nullptr;
    raw = curl_slist_append(raw, "content-type: application/json");
    raw = curl_slist_append(raw, key_header.c_str());
    raw = curl_slist_append(raw, version_header.c_str());
    for (const auto& h : extra_headers) raw = curl_slist_append(raw, h.c_str());
    std::unique_ptr<curl_slist, SlistDeleter> headers(raw);

    HttpResult result;
    char errbuf[CURL_ERROR_SIZE] = {0};

    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, json_body.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(json_body.size()));
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &result.body);
    curl_easy_setopt(curl.get(), CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(curl.get(), CURLOPT_HEADERDATA, &result.request_id);
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

}  // namespace claude
