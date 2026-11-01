#include "rag/http_client.hpp"

#include <curl/curl.h>

#include <mutex>

namespace rag {

namespace {

std::once_flag g_curl_init;

size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

}  // namespace

HttpClient::HttpClient(long timeout_seconds) : curl_(nullptr), timeout_(timeout_seconds) {
    std::call_once(g_curl_init, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
    curl_ = curl_easy_init();
    if (!curl_) throw std::runtime_error("curl_easy_init failed");
}

HttpClient::~HttpClient() {
    if (curl_) curl_easy_cleanup(static_cast<CURL*>(curl_));
}

HttpResponse HttpClient::request(const std::string& method, const std::string& url,
                                 const std::string& body) {
    CURL* c = static_cast<CURL*>(curl_);
    curl_easy_reset(c);

    HttpResponse resp;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");

    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, method.c_str());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &resp.body);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, timeout_);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    if (!body.empty() || method == "POST" || method == "PUT") {
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    }

    const CURLcode rc = curl_easy_perform(c);
    curl_slist_free_all(headers);
    if (rc != CURLE_OK) {
        throw HttpError(method + " " + url + " failed: " + curl_easy_strerror(rc), 0);
    }
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &resp.status);
    return resp;
}

json HttpClient::send_json(const std::string& method, const std::string& url,
                           const json& payload) {
    const HttpResponse r = request(method, url, payload.is_null() ? "" : payload.dump());
    if (!r.ok()) {
        throw HttpError(method + " " + url + " -> HTTP " + std::to_string(r.status) + ": " +
                            r.body,
                        r.status);
    }
    if (r.body.empty()) return json::object();
    return json::parse(r.body);
}

json HttpClient::get_json(const std::string& url) { return send_json("GET", url, nullptr); }
json HttpClient::post_json(const std::string& url, const json& p) { return send_json("POST", url, p); }
json HttpClient::put_json(const std::string& url, const json& p) { return send_json("PUT", url, p); }

}  // namespace rag
