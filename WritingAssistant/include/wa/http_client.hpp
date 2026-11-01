// HTTP abstraction. The Anthropic client depends on this interface, not on a
// concrete transport, so tests can inject a fake.
#pragma once

#include <map>
#include <stdexcept>
#include <string>

namespace wa {

struct HttpRequest {
    std::string method = "POST";
    std::string url;
    std::map<std::string, std::string> headers;
    std::string body;
    int timeoutSeconds = 600;
};

struct HttpResponse {
    int status = 0;
    std::string body;
};

// Thrown when the request could not be sent at all (DNS, TLS, timeout...).
class TransportError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class IHttpClient {
public:
    virtual ~IHttpClient() = default;
    virtual HttpResponse send(const HttpRequest& request) = 0;
};

// Sends requests by running the `curl` command-line tool. Headers (including
// the API key) are passed through a private temporary config file, never on
// the command line, so they do not show up in `ps` output.
class CurlHttpClient : public IHttpClient {
public:
    explicit CurlHttpClient(std::string curlBinary = "curl") : curl_(std::move(curlBinary)) {}
    HttpResponse send(const HttpRequest& request) override;

private:
    std::string curl_;
};

}  // namespace wa
