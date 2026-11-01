// Application configuration, read from environment variables and then
// overridden by command-line flags.
#pragma once

#include <functional>
#include <string>

namespace wa {

struct AppConfig {
    std::string provider = "auto";   // auto | anthropic | offline
    std::string apiKey;
    std::string model = "claude-opus-5-5";
    std::string baseUrl = "https://api.anthropic.com";
    std::string effort = "medium";
    bool serverSideFallbacks = true;
    int maxTokens = 16000;

    // "auto" resolves to anthropic when a key is present, otherwise offline.
    std::string resolvedProvider() const;

    using EnvLookup = std::function<const char*(const char*)>;
    static AppConfig fromEnvironment(const EnvLookup& getenv);
};

}  // namespace wa
