// chat_app.cpp
#include "chat_app.h"

#include <iostream>
#include <utility>

#include "intents.h"
#include "text_utils.h"

namespace simplechat {

ChatApp::ChatApp(Config cfg) : cfg_(std::move(cfg)) {
    state_.log.open(cfg_.logFile, cfg_.logEnabled);

    if (cfg_.backend == "claude") {
        claude_ = std::make_unique<ClaudeClient>(cfg_);
        if (!claude_->hasApiKey()) {
            std::cerr << "warning: backend = claude but " << cfg_.apiKeyEnv
                      << " is not set; using the offline rules backend.\n";
            claude_.reset();
            cfg_.backend = "rules";
        }
    } else if (cfg_.backend != "rules") {
        std::cerr << "warning: unknown backend '" << cfg_.backend << "'; using rules.\n";
        cfg_.backend = "rules";
    }
}

std::string ChatApp::reply(const std::string& input) {
    // Local commands always win; small talk is left to Claude when it is on.
    std::string text;
    if (tryRuleReply(input, state_, cfg_, /*commandsOnly=*/claude_ != nullptr, text)) {
        if (state_.clearRequested) {
            if (claude_) claude_->reset();
            state_.clearRequested = false;
        }
        return text;
    }
    if (claude_) return claude_->send(input).text;
    return cfg_.fallbackReply;
}

void ChatApp::printBanner() const {
    std::cout << "==============================\n"
              << "  Welcome to " << cfg_.botName << " v" << cfg_.version << "\n"
              << "  Backend: " << (claude_ ? "Claude (" + cfg_.model + ")" : std::string("rules"))
              << "\n==============================\n"
              << "Type 'help' for commands, 'quit' to exit.\n\n";
}

bool ChatApp::readUserInput(std::string& out) const {
    std::cout << cfg_.userPrompt << std::flush;
    return static_cast<bool>(std::getline(std::cin, out));
}

void ChatApp::printReply(const std::string& text) const {
    std::cout << cfg_.botPrompt << text << "\n";
}

void ChatApp::processTurn(const std::string& rawInput) {
    const std::string input = trim(rawInput);
    if (input.empty()) return;
    rememberInput(state_, cfg_, input);
    state_.log.write("USER", input);
    const std::string text = reply(input);
    printReply(text);
    state_.log.write("BOT", text);
}

int ChatApp::run() {
    printBanner();
    std::string line;
    while (state_.running && readUserInput(line)) processTurn(line);
    return 0;
}

}  // namespace simplechat
