// SimpleChat - PHASE 1: Configuration Extraction
// ------------------------------------------------------------
// Goal: remove every hard-coded "magic value" from the logic.
//
// What changed compared to 00_original:
//   * A single `Config` struct holds every tunable setting, with
//     sensible defaults declared in ONE place.
//   * Settings can be overridden, in increasing priority, by:
//       1. built-in defaults
//       2. a key=value config file  (default: simplechat.conf)
//       3. environment variables    (SIMPLECHAT_<KEY>)
//       4. command line             (--config <file>)
//   * The chat loop reads `cfg.xxx` instead of literals, so the bot
//     can be renamed, re-prompted or re-limited without recompiling.
//
// What did NOT change yet: main() still contains the whole chat
// loop. That is Phase 2's job.
//
// Build: g++ -std=c++17 -Wall -Wextra -o simple_chat simple_chat.cpp
// Run:   ./simple_chat --config simplechat.conf
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------
// Configuration: every setting the program uses, with its default.
// ---------------------------------------------------------------
struct Config {
    std::string botName      = "SimpleBot";
    std::string version      = "1.1";
    std::string userPrompt   = "You> ";
    std::string botPrompt    = "Bot> ";
    std::string logFile      = "simplechat.log";
    bool        logEnabled   = true;
    size_t      maxHistory   = 50;
    std::string timeFormat   = "%Y-%m-%d %H:%M:%S";
    std::string fallbackReply =
        "I'm not sure how to respond to that. Type 'help' to see what I can do.";
};

// Apply one key/value pair to the config. Unknown keys are reported.
bool applySetting(Config& cfg, const std::string& key, const std::string& value) {
    if      (key == "bot_name")       cfg.botName = value;
    else if (key == "user_prompt")    cfg.userPrompt = value;
    else if (key == "bot_prompt")     cfg.botPrompt = value;
    else if (key == "log_file")       cfg.logFile = value;
    else if (key == "log_enabled")    cfg.logEnabled = (value == "true" || value == "1" || value == "yes");
    else if (key == "max_history")    cfg.maxHistory = static_cast<size_t>(std::stoul(value));
    else if (key == "time_format")    cfg.timeFormat = value;
    else if (key == "fallback_reply") cfg.fallbackReply = value;
    else return false;
    return true;
}

// Load "key = value" lines. '#' starts a comment. Missing file is OK.
// Values may be wrapped in double quotes to keep leading/trailing spaces.
void loadConfigFile(Config& cfg, const std::string& path) {
    std::ifstream in(path);
    if (!in) return;
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        size_t b = line.find_first_not_of(" \t\r");
        if (b == std::string::npos || line[b] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            std::cerr << path << ":" << lineNo << ": expected key = value\n";
            continue;
        }
        std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t\r") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t\r") + 1);
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
            value = value.substr(1, value.size() - 2);
        try {
            if (!applySetting(cfg, key, value))
                std::cerr << path << ":" << lineNo << ": unknown setting '" << key << "'\n";
        } catch (const std::exception&) {
            std::cerr << path << ":" << lineNo << ": invalid value for '" << key << "'\n";
        }
    }
}

// SIMPLECHAT_BOT_NAME=Ada overrides bot_name, and so on.
void loadEnvironment(Config& cfg) {
    const char* keys[] = {"bot_name", "user_prompt", "bot_prompt", "log_file",
                          "log_enabled", "max_history", "time_format", "fallback_reply"};
    for (const char* key : keys) {
        std::string env = "SIMPLECHAT_";
        for (const char* p = key; *p; ++p) env += static_cast<char>(std::toupper(*p));
        if (const char* v = std::getenv(env.c_str())) {
            try { applySetting(cfg, key, v); }
            catch (const std::exception&) { std::cerr << "invalid value in " << env << "\n"; }
        }
    }
}

int main(int argc, char** argv) {
    std::string configPath = "simplechat.conf";
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) configPath = argv[++i];
    }

    Config cfg;
    loadConfigFile(cfg, configPath);
    loadEnvironment(cfg);

    // ---- Everything below is still the old monolithic loop, ----
    // ---- but with magic values replaced by cfg fields.       ----
    std::vector<std::string> history;
    std::string userName;
    std::ofstream log;
    if (cfg.logEnabled) log.open(cfg.logFile, std::ios::app);

    std::cout << "==============================\n";
    std::cout << "  Welcome to " << cfg.botName << " v" << cfg.version << "\n";
    std::cout << "==============================\n";
    std::cout << "Type 'help' for commands, 'quit' to exit.\n\n";

    while (true) {
        std::cout << cfg.userPrompt;
        std::string line;
        if (!std::getline(std::cin, line)) break;

        size_t b = line.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) continue;
        size_t e = line.find_last_not_of(" \t\r\n");
        line = line.substr(b, e - b + 1);

        std::string lower = line;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        if (cfg.maxHistory > 0 && history.size() >= cfg.maxHistory) history.erase(history.begin());
        history.push_back(line);
        if (log) log << "USER: " << line << "\n";

        std::string reply;
        if (lower == "quit" || lower == "exit" || lower == "bye") {
            std::cout << cfg.botPrompt << "Goodbye" << (userName.empty() ? "" : ", " + userName) << "!\n";
            if (log) log << "BOT: Goodbye\n";
            break;
        } else if (lower == "help") {
            reply = "Commands: help, history, time, calc <a> <op> <b>, my name is <name>, quit";
        } else if (lower == "history") {
            std::ostringstream os;
            os << "You said " << history.size() << " thing(s):";
            for (size_t i = 0; i < history.size(); ++i) os << "\n   " << i + 1 << ". " << history[i];
            reply = os.str();
        } else if (lower == "time") {
            std::time_t now = std::time(nullptr);
            char buf[128];
            std::strftime(buf, sizeof buf, cfg.timeFormat.c_str(), std::localtime(&now));
            reply = std::string("The time is ") + buf;
        } else if (lower.rfind("calc ", 0) == 0) {
            std::istringstream in(line.substr(5));
            double a, bb; char op;
            if (in >> a >> op >> bb) {
                if (op == '+') reply = "Result: " + std::to_string(a + bb);
                else if (op == '-') reply = "Result: " + std::to_string(a - bb);
                else if (op == '*') reply = "Result: " + std::to_string(a * bb);
                else if (op == '/') reply = bb == 0 ? "Cannot divide by zero." : "Result: " + std::to_string(a / bb);
                else reply = "Unknown operator. Use + - * /";
            } else {
                reply = "Usage: calc <a> <op> <b>";
            }
        } else if (lower.rfind("my name is ", 0) == 0) {
            userName = line.substr(11);
            reply = "Nice to meet you, " + userName + "!";
        } else if (lower.find("hello") != std::string::npos || lower.find("hi") == 0) {
            reply = userName.empty() ? "Hello! What's your name?" : "Hello again, " + userName + "!";
        } else if (lower.find("how are you") != std::string::npos) {
            reply = "I'm just a program, but I'm running great. Thanks for asking!";
        } else if (lower.find("your name") != std::string::npos) {
            reply = "I'm " + cfg.botName + ".";
        } else {
            reply = cfg.fallbackReply;
        }

        std::cout << cfg.botPrompt << reply << "\n";
        if (log) log << "BOT: " << reply << "\n";
    }
    return 0;
}
