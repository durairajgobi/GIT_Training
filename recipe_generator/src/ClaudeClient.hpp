#pragma once

#include "Ingredient.hpp"
#include "RecipeEngine.hpp"

#include <stdexcept>
#include <string>

namespace chef {

class ClaudeError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Calls the Claude Messages API over raw HTTPS. There is no official C++ SDK,
// so the request is sent with the system `curl` binary; the API key is passed
// through a private temp file so it never appears on a command line.
class ClaudeClient {
public:
    // Reads ANTHROPIC_API_KEY (required) and ANTHROPIC_BASE_URL (optional).
    ClaudeClient();

    static bool available();

    // Returns the text of Claude's reply. Throws ClaudeError on failure or refusal.
    std::string complete(const std::string& system, const std::string& user) const;

    std::string model = "claude-opus-5-5";
    std::string effort = "medium";
    int maxTokens = 16000;

private:
    std::string apiKey_;
    std::string baseUrl_;
};

// System prompt and user prompt for the Claude-powered chef.
std::string chefSystemPrompt();
std::string chefUserPrompt(const Preferences& prefs, const Recipe& draft);

} // namespace chef
