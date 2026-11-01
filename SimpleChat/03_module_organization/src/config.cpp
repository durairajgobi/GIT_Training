// config.cpp - Configuration loading (Phase 1 logic, now in its own module).
#include "config.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>

#include "text_utils.h"

namespace simplechat {

namespace {
const char* const kKeys[] = {
    "bot_name", "user_prompt", "bot_prompt", "log_file", "log_enabled", "max_history",
    "time_format", "fallback_reply", "backend", "api_url", "api_version", "api_key_env",
    "model", "max_tokens", "effort", "server_fallback", "timeout_seconds", "system_prompt"};
}

bool applySetting(Config& cfg, const std::string& key, const std::string& value) {
    if      (key == "bot_name")        cfg.botName = value;
    else if (key == "user_prompt")     cfg.userPrompt = value;
    else if (key == "bot_prompt")      cfg.botPrompt = value;
    else if (key == "log_file")        cfg.logFile = value;
    else if (key == "log_enabled")     cfg.logEnabled = parseBool(value);
    else if (key == "max_history")     cfg.maxHistory = static_cast<std::size_t>(std::stoul(value));
    else if (key == "time_format")     cfg.timeFormat = value;
    else if (key == "fallback_reply")  cfg.fallbackReply = value;
    else if (key == "backend")         cfg.backend = toLower(value);
    else if (key == "api_url")         cfg.apiUrl = value;
    else if (key == "api_version")     cfg.apiVersion = value;
    else if (key == "api_key_env")     cfg.apiKeyEnv = value;
    else if (key == "model")           cfg.model = value;
    else if (key == "max_tokens")      cfg.maxTokens = std::stoi(value);
    else if (key == "effort")          cfg.effort = toLower(value);
    else if (key == "server_fallback") cfg.serverFallback = parseBool(value);
    else if (key == "timeout_seconds") cfg.timeoutSeconds = std::stoi(value);
    else if (key == "system_prompt")   cfg.systemPrompt = value;
    else return false;
    return true;
}

void loadConfigFile(Config& cfg, const std::string& path) {
    std::ifstream in(path);
    if (!in) return;  // a missing file just means "use defaults"
    std::string line;
    for (int lineNo = 1; std::getline(in, line); ++lineNo) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            std::cerr << path << ":" << lineNo << ": expected key = value\n";
            continue;
        }
        const std::string key = trim(line.substr(0, eq));
        const std::string value = unquote(trim(line.substr(eq + 1)));
        try {
            if (!applySetting(cfg, key, value))
                std::cerr << path << ":" << lineNo << ": unknown setting '" << key << "'\n";
        } catch (const std::exception&) {
            std::cerr << path << ":" << lineNo << ": invalid value for '" << key << "'\n";
        }
    }
}

void loadEnvironment(Config& cfg) {
    for (const char* key : kKeys) {
        std::string env = "SIMPLECHAT_";
        for (const char* p = key; *p; ++p) env += static_cast<char>(std::toupper(*p));
        if (const char* v = std::getenv(env.c_str())) {
            try {
                applySetting(cfg, key, v);
            } catch (const std::exception&) {
                std::cerr << "invalid value in " << env << "\n";
            }
        }
    }
}

std::string parseConfigPath(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "--config") return argv[i + 1];
    return "simplechat.conf";
}

Config loadConfig(int argc, char** argv) {
    Config cfg;
    loadConfigFile(cfg, parseConfigPath(argc, argv));
    loadEnvironment(cfg);
    return cfg;
}

}  // namespace simplechat
