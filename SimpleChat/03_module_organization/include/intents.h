// intents.h - Rule-based replies (the Phase 2 handler table, as a module).
#pragma once
#include <string>

#include "chat_state.h"
#include "config.h"

namespace simplechat {

// Pure helper, exposed so it can be unit tested.
std::string calculate(double a, char op, double b);

// Tries every rule in order. Returns true and fills `reply` on a match.
// When `commandsOnly` is true, small-talk rules are skipped so that an
// AI backend can answer conversational messages instead.
bool tryRuleReply(const std::string& input, ChatState& state, const Config& cfg,
                  bool commandsOnly, std::string& reply);

}  // namespace simplechat
