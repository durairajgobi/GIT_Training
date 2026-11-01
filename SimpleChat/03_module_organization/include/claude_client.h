// claude_client.h - Optional AI backend: the Claude Messages API.
//
// C++ has no official Anthropic SDK, so this module talks raw HTTP by
// running the `curl` command-line tool. The API key is read from the
// environment variable named by cfg.apiKeyEnv and is written only to a
// private (0600) temp file - never to the command line or the log.
#pragma once
#include <string>
#include <vector>

#include "config.h"
#include "json.h"

namespace simplechat {

struct ClaudeReply {
    bool ok = false;
    std::string text;   // the assistant's reply, or an error message when !ok
};

class ClaudeClient {
public:
    explicit ClaudeClient(const Config& cfg);

    // True when an API key was found in the environment.
    bool hasApiKey() const { return !apiKey_.empty(); }

    // Sends `userText` along with the whole conversation so far.
    ClaudeReply send(const std::string& userText);

    // Forget the conversation (used by the "clear" command).
    void reset() { messages_.clear(); }

private:
    const Config& cfg_;
    std::string apiKey_;
    std::vector<json::Value> messages_;  // alternating user/assistant turns

    json::Value buildRequest() const;
    bool postJson(const std::string& body, std::string& responseBody, int& httpStatus,
                  std::string& error) const;
};

}  // namespace simplechat
