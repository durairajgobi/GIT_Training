#include "wa/config.hpp"

#include <cstdlib>

namespace wa {

std::string AppConfig::resolvedProvider() const {
    if (provider != "auto") return provider;
    return apiKey.empty() ? "offline" : "anthropic";
}

AppConfig AppConfig::fromEnvironment(const EnvLookup& getenv) {
    AppConfig c;
    auto read = [&](const char* name, std::string& target) {
        if (const char* v = getenv(name); v && *v) target = v;
    };
    read("ANTHROPIC_API_KEY", c.apiKey);
    read("ANTHROPIC_BASE_URL", c.baseUrl);
    read("WA_PROVIDER", c.provider);
    read("WA_MODEL", c.model);
    read("WA_EFFORT", c.effort);
    if (const char* v = getenv("WA_MAX_TOKENS"); v && *v) c.maxTokens = std::atoi(v);
    if (const char* v = getenv("WA_FALLBACKS"); v && std::string(v) == "0") c.serverSideFallbacks = false;
    return c;
}

}  // namespace wa
