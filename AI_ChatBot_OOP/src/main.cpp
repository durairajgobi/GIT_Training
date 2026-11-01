// Phase 3 - Entry point. main() is now a thin UI loop: all HTTP details live
// in HttpClientHelper and all conversation/API logic lives in ChatClient.

#include "ChatClient.h"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

namespace {

void printHelp() {
    std::cout << "Commands: /reset (clear conversation), /help, /quit\n";
}

}  // namespace

int main() {
    const char* key = std::getenv("ANTHROPIC_API_KEY");
    if (!key || !*key) {
        std::cerr << "Error: set the ANTHROPIC_API_KEY environment variable.\n";
        return EXIT_FAILURE;
    }

    try {
        ChatConfig config;
        config.apiKey = key;
        if (const char* model = std::getenv("CHATBOT_MODEL"); model && *model) {
            config.model = model;
        }

        ChatClient chat(config);

        std::cout << "AI ChatBot (" << chat.config().model << ")\n";
        printHelp();

        std::string line;
        while (true) {
            std::cout << "\nYou: " << std::flush;
            if (!std::getline(std::cin, line)) break;
            if (line.empty()) continue;

            if (line == "/quit" || line == "/exit") break;
            if (line == "/help") { printHelp(); continue; }
            if (line == "/reset") {
                chat.clearHistory();
                std::cout << "(conversation cleared)\n";
                continue;
            }

            try {
                const std::string reply = chat.sendMessage(line);
                std::cout << "\nBot: " << reply << "\n";
            } catch (const std::exception& e) {
                std::cerr << "\n[error] " << e.what() << "\n";
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    std::cout << "Goodbye!\n";
    return EXIT_SUCCESS;
}
