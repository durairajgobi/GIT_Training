// chat_app.h - Console front end: banner, input, reply routing, loop.
#pragma once
#include <memory>
#include <string>

#include "chat_state.h"
#include "claude_client.h"
#include "config.h"

namespace simplechat {

class ChatApp {
public:
    explicit ChatApp(Config cfg);
    int run();  // returns the process exit code

    // One user turn -> one reply. Public so it can be tested without a console.
    std::string reply(const std::string& input);
    bool running() const { return state_.running; }

private:
    Config cfg_;
    ChatState state_;
    std::unique_ptr<ClaudeClient> claude_;  // null when backend = rules

    void printBanner() const;
    bool readUserInput(std::string& out) const;
    void printReply(const std::string& reply) const;
    void processTurn(const std::string& rawInput);
};

}  // namespace simplechat
