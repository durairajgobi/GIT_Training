// config.h - Application settings and how they are loaded.
#pragma once
#include <cstddef>
#include <string>

namespace simplechat {

struct Config {
    // --- Identity & console ---
    std::string botName       = "SimpleBot";
    std::string version       = "1.3";
    std::string userPrompt    = "You> ";
    std::string botPrompt     = "Bot> ";

    // --- Logging & history ---
    std::string logFile       = "simplechat.log";
    bool        logEnabled    = true;
    std::size_t maxHistory    = 50;
    std::string timeFormat    = "%Y-%m-%d %H:%M:%S";
    std::string fallbackReply =
        "I'm not sure how to respond to that. Type 'help' to see what I can do.";

    // --- Reply backend: "rules" (offline) or "claude" (Claude API) ---
    std::string backend       = "rules";

    // --- Claude API settings (used when backend = claude) ---
    std::string apiUrl        = "https://api.anthropic.com/v1/messages";
    std::string apiVersion    = "2026-10-21";
    std::string apiKeyEnv     = "ANTHROPIC_API_KEY";  // name of the env var, never the key itself
    std::string model         = "claude-opus-5-5";
    int         maxTokens     = 16000;
    std::string effort        = "medium";             // low | medium | high | xhigh | max
    bool        serverFallback = true;                // retry refused requests on a fallback model
    int         timeoutSeconds = 600;
    std::string systemPrompt  =
        "You are SimpleBot, a friendly and concise assistant in a terminal chat. "
        "Answer in plain text without Markdown.";
};

// Apply a single key/value. Returns false for an unknown key.
// Throws std::exception for a value that cannot be parsed.
bool applySetting(Config& cfg, const std::string& key, const std::string& value);

// Layered loading: defaults -> config file -> SIMPLECHAT_* env vars.
void loadConfigFile(Config& cfg, const std::string& path);
void loadEnvironment(Config& cfg);
std::string parseConfigPath(int argc, char** argv);
Config loadConfig(int argc, char** argv);

}  // namespace simplechat
