// claude_client.cpp
#include "claude_client.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <unistd.h>

#include "text_utils.h"

namespace simplechat {

namespace {

// Beta header that enables server-side fallback when a request is refused.
constexpr const char* kFallbackBeta = "server-side-fallback-2026-07-01";

// Quote an argument for /bin/sh: wrap in '...' and escape embedded quotes.
std::string shellQuote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    return out + "'";
}

// Creates a private temp file containing `content`. Returns its path or "".
std::string writeTempFile(const std::string& content) {
    const char* dir = std::getenv("TMPDIR");
    std::string templ = std::string(dir && *dir ? dir : "/tmp") + "/simplechat-XXXXXX";
    std::vector<char> buf(templ.begin(), templ.end());
    buf.push_back('\0');
    const int fd = mkstemp(buf.data());  // created with mode 0600
    if (fd < 0) return "";
    const char* p = content.data();
    std::size_t left = content.size();
    while (left > 0) {
        const ssize_t n = ::write(fd, p, left);
        if (n <= 0) { ::close(fd); ::unlink(buf.data()); return ""; }
        p += n;
        left -= static_cast<std::size_t>(n);
    }
    ::close(fd);
    return buf.data();
}

// Deletes the temp file when it goes out of scope.
struct TempFile {
    std::string path;
    explicit TempFile(const std::string& content) : path(writeTempFile(content)) {}
    ~TempFile() { if (!path.empty()) ::unlink(path.c_str()); }
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
};

json::Value textMessage(const std::string& role, const std::string& text) {
    json::Value block = json::Value::object();
    block.set("type", "text").set("text", text);
    json::Value content = json::Value::array();
    content.push(block);
    json::Value msg = json::Value::object();
    msg.set("role", role).set("content", content);
    return msg;
}

}  // namespace

ClaudeClient::ClaudeClient(const Config& cfg) : cfg_(cfg) {
    if (const char* key = std::getenv(cfg.apiKeyEnv.c_str())) apiKey_ = key;
}

json::Value ClaudeClient::buildRequest() const {
    json::Value messages = json::Value::array();
    for (const auto& m : messages_) messages.push(m);

    json::Value outputConfig = json::Value::object();
    outputConfig.set("effort", cfg_.effort);

    json::Value req = json::Value::object();
    req.set("model", cfg_.model)
        .set("max_tokens", cfg_.maxTokens)
        .set("system", cfg_.systemPrompt)
        .set("messages", messages)
        .set("output_config", outputConfig);
    if (cfg_.serverFallback) req.set("fallbacks", "default");
    return req;
}

bool ClaudeClient::postJson(const std::string& body, std::string& responseBody, int& httpStatus,
                            std::string& error) const {
    std::string headers =
        "content-type: application/json\n"
        "x-api-key: " + apiKey_ + "\n"
        "anthropic-version: " + cfg_.apiVersion + "\n";
    if (cfg_.serverFallback) headers += std::string("anthropic-beta: ") + kFallbackBeta + "\n";

    TempFile headerFile(headers);
    TempFile bodyFile(body);
    if (headerFile.path.empty() || bodyFile.path.empty()) {
        error = "could not create temporary files";
        return false;
    }

    // -w appends the HTTP status on its own line after the body.
    const std::string cmd = "curl -sS -X POST --max-time " + std::to_string(cfg_.timeoutSeconds) +
                            " -H @" + shellQuote(headerFile.path) +
                            " --data-binary @" + shellQuote(bodyFile.path) +
                            " -w '\\n%{http_code}' " + shellQuote(cfg_.apiUrl) + " 2>&1";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        error = "could not run curl";
        return false;
    }
    std::string output;
    std::array<char, 4096> chunk{};
    std::size_t n;
    while ((n = std::fread(chunk.data(), 1, chunk.size(), pipe)) > 0) output.append(chunk.data(), n);
    const int rc = pclose(pipe);

    const auto nl = output.find_last_of('\n');
    const std::string statusText = nl == std::string::npos ? output : output.substr(nl + 1);
    httpStatus = std::atoi(statusText.c_str());
    responseBody = nl == std::string::npos ? "" : output.substr(0, nl);
    if (rc != 0 || httpStatus == 0) {
        error = "network request failed: " + trim(responseBody.empty() ? output : responseBody);
        return false;
    }
    return true;
}

ClaudeReply ClaudeClient::send(const std::string& userText) {
    ClaudeReply reply;
    if (apiKey_.empty()) {
        reply.text = "No API key: set the " + cfg_.apiKeyEnv + " environment variable.";
        return reply;
    }

    messages_.push_back(textMessage("user", userText));

    std::string body, error;
    int status = 0;
    if (!postJson(buildRequest().dump(), body, status, error)) {
        messages_.pop_back();  // keep the conversation valid for the next try
        reply.text = error;
        return reply;
    }

    json::Value response;
    try {
        response = json::parse(body);
    } catch (const json::ParseError& e) {
        messages_.pop_back();
        reply.text = "HTTP " + std::to_string(status) + ": unreadable response (" + e.what() + ")";
        return reply;
    }

    if (status != 200 || response["type"].asString() == "error") {
        messages_.pop_back();
        const std::string& msg = response["error"]["message"].asString();
        reply.text = "API error (HTTP " + std::to_string(status) + "): " + (msg.empty() ? body : msg);
        return reply;
    }

    const std::string& stopReason = response["stop_reason"].asString();
    if (stopReason == "refusal") {
        messages_.pop_back();
        reply.text = "Sorry, I can't help with that request.";
        return reply;
    }

    // Collect the text blocks; other block types (e.g. thinking) are not shown.
    const json::Value& content = response["content"];
    for (const auto& block : content.items())
        if (block["type"].asString() == "text") reply.text += block["text"].asString();
    if (stopReason == "max_tokens") reply.text += "\n[reply truncated: max_tokens reached]";

    // Send the assistant content back unchanged on the next turn.
    json::Value assistant = json::Value::object();
    assistant.set("role", "assistant").set("content", content);
    messages_.push_back(assistant);

    reply.ok = true;
    return reply;
}

}  // namespace simplechat
