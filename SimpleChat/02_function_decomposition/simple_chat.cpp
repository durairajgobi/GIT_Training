// SimpleChat - PHASE 2: Function Decomposition
// ------------------------------------------------------------
// Goal: break the giant main() into small, single-purpose functions.
//
// What changed compared to 01_config_extraction:
//   * Text helpers (trim, toLower, startsWith, contains) are reusable
//     functions instead of inline copy-paste.
//   * All mutable session data lives in one `ChatState` struct that is
//     passed explicitly - no hidden globals.
//   * Each feature is its own handler function with the same signature:
//         std::string handleX(const std::string& input, ChatState&, const Config&)
//   * The if/else chain became a data table of {matcher, handler}
//     pairs. Adding a feature = write one function + add one row.
//   * main() is now ~10 lines that read like a table of contents.
//
// Still one file. Phase 3 splits these functions into modules.
//
// Build: g++ -std=c++17 -Wall -Wextra -o simple_chat simple_chat.cpp
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// =============================== Configuration ===============================

struct Config {
    std::string botName      = "SimpleBot";
    std::string version      = "1.2";
    std::string userPrompt   = "You> ";
    std::string botPrompt    = "Bot> ";
    std::string logFile      = "simplechat.log";
    bool        logEnabled   = true;
    size_t      maxHistory   = 50;
    std::string timeFormat   = "%Y-%m-%d %H:%M:%S";
    std::string fallbackReply =
        "I'm not sure how to respond to that. Type 'help' to see what I can do.";
};

// =============================== Text helpers ================================

std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool startsWith(const std::string& s, const std::string& prefix) {
    return s.rfind(prefix, 0) == 0;
}

bool contains(const std::string& s, const std::string& needle) {
    return s.find(needle) != std::string::npos;
}

std::string unquote(const std::string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') return s.substr(1, s.size() - 2);
    return s;
}

bool parseBool(const std::string& v) {
    std::string l = toLower(v);
    return l == "true" || l == "1" || l == "yes" || l == "on";
}

// ============================ Config loading =================================

bool applySetting(Config& cfg, const std::string& key, const std::string& value) {
    if      (key == "bot_name")       cfg.botName = value;
    else if (key == "user_prompt")    cfg.userPrompt = value;
    else if (key == "bot_prompt")     cfg.botPrompt = value;
    else if (key == "log_file")       cfg.logFile = value;
    else if (key == "log_enabled")    cfg.logEnabled = parseBool(value);
    else if (key == "max_history")    cfg.maxHistory = static_cast<size_t>(std::stoul(value));
    else if (key == "time_format")    cfg.timeFormat = value;
    else if (key == "fallback_reply") cfg.fallbackReply = value;
    else return false;
    return true;
}

void loadConfigFile(Config& cfg, const std::string& path) {
    std::ifstream in(path);
    if (!in) return;
    std::string line;
    for (int lineNo = 1; std::getline(in, line); ++lineNo) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            std::cerr << path << ":" << lineNo << ": expected key = value\n";
            continue;
        }
        std::string key = trim(line.substr(0, eq));
        std::string value = unquote(trim(line.substr(eq + 1)));
        try {
            if (!applySetting(cfg, key, value))
                std::cerr << path << ":" << lineNo << ": unknown setting '" << key << "'\n";
        } catch (const std::exception&) {
            std::cerr << path << ":" << lineNo << ": invalid value for '" << key << "'\n";
        }
    }
}

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

// ================================ Session state ==============================

struct ChatState {
    std::vector<std::string> history;
    std::string userName;
    std::ofstream log;
    bool running = true;
};

void openLog(ChatState& state, const Config& cfg) {
    if (cfg.logEnabled) state.log.open(cfg.logFile, std::ios::app);
}

void logLine(ChatState& state, const std::string& who, const std::string& text) {
    if (state.log) state.log << who << ": " << text << "\n";
}

void rememberInput(ChatState& state, const Config& cfg, const std::string& input) {
    if (cfg.maxHistory > 0 && state.history.size() >= cfg.maxHistory)
        state.history.erase(state.history.begin());
    state.history.push_back(input);
}

// ============================== Intent handlers ==============================
// Every handler has the same shape so they can live in one table.

using Handler = std::string (*)(const std::string& input, ChatState&, const Config&);
using Matcher = bool (*)(const std::string& lowerInput);

std::string handleQuit(const std::string&, ChatState& state, const Config&) {
    state.running = false;
    return "Goodbye" + (state.userName.empty() ? "" : ", " + state.userName) + "!";
}

std::string handleHelp(const std::string&, ChatState&, const Config&) {
    return "Commands: help, history, time, calc <a> <op> <b>, my name is <name>, quit";
}

std::string handleHistory(const std::string&, ChatState& state, const Config&) {
    std::ostringstream os;
    os << "You said " << state.history.size() << " thing(s):";
    for (size_t i = 0; i < state.history.size(); ++i)
        os << "\n   " << i + 1 << ". " << state.history[i];
    return os.str();
}

std::string formatNow(const std::string& fmt) {
    std::time_t now = std::time(nullptr);
    char buf[128];
    if (std::strftime(buf, sizeof buf, fmt.c_str(), std::localtime(&now)) == 0) return "?";
    return buf;
}

std::string handleTime(const std::string&, ChatState&, const Config& cfg) {
    return "The time is " + formatNow(cfg.timeFormat);
}

// Pure function: easy to unit test in isolation.
std::string calculate(double a, char op, double b) {
    switch (op) {
        case '+': return "Result: " + std::to_string(a + b);
        case '-': return "Result: " + std::to_string(a - b);
        case '*': return "Result: " + std::to_string(a * b);
        case '/': return b == 0 ? "Cannot divide by zero." : "Result: " + std::to_string(a / b);
        default:  return "Unknown operator. Use + - * /";
    }
}

std::string handleCalc(const std::string& input, ChatState&, const Config&) {
    std::istringstream in(input.substr(5));
    double a, b;
    char op;
    if (!(in >> a >> op >> b)) return "Usage: calc <a> <op> <b>";
    return calculate(a, op, b);
}

std::string handleSetName(const std::string& input, ChatState& state, const Config&) {
    state.userName = trim(input.substr(11));
    return "Nice to meet you, " + state.userName + "!";
}

std::string handleGreeting(const std::string&, ChatState& state, const Config&) {
    return state.userName.empty() ? "Hello! What's your name?" : "Hello again, " + state.userName + "!";
}

std::string handleHowAreYou(const std::string&, ChatState&, const Config&) {
    return "I'm just a program, but I'm running great. Thanks for asking!";
}

std::string handleBotName(const std::string&, ChatState&, const Config& cfg) {
    return "I'm " + cfg.botName + ".";
}

// --- matchers -----------------------------------------------------------------
bool isQuit(const std::string& s)     { return s == "quit" || s == "exit" || s == "bye"; }
bool isHelp(const std::string& s)     { return s == "help"; }
bool isHistory(const std::string& s)  { return s == "history"; }
bool isTime(const std::string& s)     { return s == "time"; }
bool isCalc(const std::string& s)     { return startsWith(s, "calc "); }
bool isSetName(const std::string& s)  { return startsWith(s, "my name is "); }
bool isGreeting(const std::string& s) { return contains(s, "hello") || startsWith(s, "hi"); }
bool isHowAreYou(const std::string& s){ return contains(s, "how are you"); }
bool isBotName(const std::string& s)  { return contains(s, "your name"); }

struct Intent {
    Matcher matches;
    Handler handle;
};

// Order matters: first match wins.
const std::vector<Intent>& intents() {
    static const std::vector<Intent> table = {
        {isQuit, handleQuit},       {isHelp, handleHelp},
        {isHistory, handleHistory}, {isTime, handleTime},
        {isCalc, handleCalc},       {isSetName, handleSetName},
        {isGreeting, handleGreeting}, {isHowAreYou, handleHowAreYou},
        {isBotName, handleBotName},
    };
    return table;
}

std::string generateReply(const std::string& input, ChatState& state, const Config& cfg) {
    const std::string lower = toLower(input);
    for (const Intent& intent : intents())
        if (intent.matches(lower)) return intent.handle(input, state, cfg);
    return cfg.fallbackReply;
}

// ================================ Console I/O ================================

void printBanner(const Config& cfg) {
    std::cout << "==============================\n"
              << "  Welcome to " << cfg.botName << " v" << cfg.version << "\n"
              << "==============================\n"
              << "Type 'help' for commands, 'quit' to exit.\n\n";
}

bool readUserInput(const Config& cfg, std::string& out) {
    std::cout << cfg.userPrompt;
    return static_cast<bool>(std::getline(std::cin, out));
}

void printReply(const Config& cfg, const std::string& reply) {
    std::cout << cfg.botPrompt << reply << "\n";
}

// ================================= Chat loop =================================

void processTurn(const std::string& rawInput, ChatState& state, const Config& cfg) {
    const std::string input = trim(rawInput);
    if (input.empty()) return;
    rememberInput(state, cfg, input);
    logLine(state, "USER", input);
    const std::string reply = generateReply(input, state, cfg);
    printReply(cfg, reply);
    logLine(state, "BOT", reply);
}

void runChatLoop(ChatState& state, const Config& cfg) {
    std::string line;
    while (state.running && readUserInput(cfg, line)) processTurn(line, state, cfg);
}

int main(int argc, char** argv) {
    const Config cfg = loadConfig(argc, argv);
    ChatState state;
    openLog(state, cfg);
    printBanner(cfg);
    runChatLoop(state, cfg);
    return 0;
}
