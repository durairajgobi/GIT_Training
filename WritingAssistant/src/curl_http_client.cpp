#include "wa/http_client.hpp"

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cctype>
#include <iterator>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

namespace wa {

namespace {

// RAII temp file created with mkstemp (mode 0600).
class TempFile {
public:
    TempFile() {
        const char* dir = std::getenv("TMPDIR");
        path_ = std::string(dir && *dir ? dir : "/tmp") + "/wa-XXXXXX";
        int fd = ::mkstemp(path_.data());
        if (fd < 0) throw TransportError("could not create temporary file");
        ::close(fd);
    }
    ~TempFile() { ::unlink(path_.c_str()); }
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;

    const std::string& path() const { return path_; }
    void write(const std::string& data) const {
        std::ofstream out(path_, std::ios::binary | std::ios::trunc);
        out << data;
        if (!out) throw TransportError("could not write temporary file");
    }

private:
    std::string path_;
};

// Quote a value for a curl config file (double-quoted, backslash escapes).
std::string curlConfigQuote(const std::string& v) {
    std::string out = "\"";
    for (char c : v) {
        if (c == '"' || c == '\\') out += '\\';
        if (c == '\n' || c == '\r') continue;  // header values may not contain newlines
        out += c;
    }
    return out + "\"";
}

// Quote a path for /bin/sh.
std::string shellQuote(const std::string& v) {
    std::string out = "'";
    for (char c : v) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    return out + "'";
}

}  // namespace

HttpResponse CurlHttpClient::send(const HttpRequest& request) {
    TempFile body, config, output;
    body.write(request.body);

    std::string cfg;
    cfg += "url = " + curlConfigQuote(request.url) + "\n";
    cfg += "request = " + curlConfigQuote(request.method) + "\n";
    cfg += "max-time = " + std::to_string(request.timeoutSeconds) + "\n";
    cfg += "silent\nshow-error\n";
    for (const auto& [k, v] : request.headers) cfg += "header = " + curlConfigQuote(k + ": " + v) + "\n";
    cfg += "data-binary = " + curlConfigQuote("@" + body.path()) + "\n";
    cfg += "output = " + curlConfigQuote(output.path()) + "\n";
    cfg += "write-out = \"%{http_code}\"\n";
    config.write(cfg);

    std::string cmd = shellQuote(curl_) + " --config " + shellQuote(config.path()) + " 2>&1";
    FILE* pipe = ::popen(cmd.c_str(), "r");
    if (!pipe) throw TransportError("failed to start curl");

    std::string captured;
    std::array<char, 4096> buf{};
    size_t n;
    while ((n = std::fread(buf.data(), 1, buf.size(), pipe)) > 0) captured.append(buf.data(), n);
    int rc = ::pclose(pipe);

    // write-out prints the status code last; anything before it is curl's stderr.
    size_t digits = captured.size();
    while (digits > 0 && std::isdigit(static_cast<unsigned char>(captured[digits - 1]))) --digits;
    const int status = std::atoi(captured.c_str() + digits);
    std::string diagnostics = captured.substr(0, digits);
    while (!diagnostics.empty() && std::isspace(static_cast<unsigned char>(diagnostics.back()))) diagnostics.pop_back();

    int exitCode = (rc != -1 && WIFEXITED(rc)) ? WEXITSTATUS(rc) : -1;
    if (exitCode != 0) {
        throw TransportError("curl failed (exit " + std::to_string(exitCode) + "): " + diagnostics);
    }

    HttpResponse resp;
    resp.status = status;

    std::ifstream in(output.path(), std::ios::binary);
    resp.body.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (resp.status == 0) throw TransportError("no HTTP response received: " + diagnostics);
    return resp;
}

}  // namespace wa
