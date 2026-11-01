// chat_state.h - Everything that changes during one chat session.
#pragma once
#include <string>
#include <vector>

#include "config.h"
#include "logger.h"

namespace simplechat {

struct ChatState {
    std::vector<std::string> history;  // what the user typed, oldest first
    std::string userName;
    Logger log;
    bool running = true;
    bool clearRequested = false;       // set by the "clear" command
};

// Adds `input` to history, dropping the oldest entry beyond cfg.maxHistory.
void rememberInput(ChatState& state, const Config& cfg, const std::string& input);

}  // namespace simplechat
