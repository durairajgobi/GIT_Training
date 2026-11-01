// chat_state.cpp
#include "chat_state.h"

namespace simplechat {

void rememberInput(ChatState& state, const Config& cfg, const std::string& input) {
    if (cfg.maxHistory > 0 && state.history.size() >= cfg.maxHistory)
        state.history.erase(state.history.begin());
    state.history.push_back(input);
}

}  // namespace simplechat
