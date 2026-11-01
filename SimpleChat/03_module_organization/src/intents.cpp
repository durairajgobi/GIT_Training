// intents.cpp - Each feature is one handler + one row in the table.
#include "intents.h"

#include <sstream>
#include <vector>

#include "text_utils.h"

namespace simplechat {

namespace {

using Handler = std::string (*)(const std::string& input, ChatState&, const Config&);
using Matcher = bool (*)(const std::string& lowerInput);

struct Intent {
    Matcher matches;
    Handler handle;
    bool isCommand;  // commands always run locally, even with the Claude backend
};

// ---- handlers ------------------------------------------------------------
std::string handleQuit(const std::string&, ChatState& state, const Config&) {
    state.running = false;
    return "Goodbye" + (state.userName.empty() ? "" : ", " + state.userName) + "!";
}

std::string handleHelp(const std::string&, ChatState&, const Config& cfg) {
    std::string text =
        "Commands: help, history, clear, time, calc <a> <op> <b>, my name is <name>, quit";
    if (cfg.backend == "claude") text += "\nAnything else is answered by " + cfg.model + ".";
    return text;
}

std::string handleHistory(const std::string&, ChatState& state, const Config&) {
    std::ostringstream os;
    os << "You said " << state.history.size() << " thing(s):";
    for (std::size_t i = 0; i < state.history.size(); ++i)
        os << "\n   " << i + 1 << ". " << state.history[i];
    return os.str();
}

std::string handleClear(const std::string&, ChatState& state, const Config&) {
    state.history.clear();
    state.clearRequested = true;
    return "Conversation cleared.";
}

std::string handleTime(const std::string&, ChatState&, const Config& cfg) {
    return "The time is " + formatNow(cfg.timeFormat);
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
    return state.userName.empty() ? "Hello! What's your name?"
                                  : "Hello again, " + state.userName + "!";
}

std::string handleHowAreYou(const std::string&, ChatState&, const Config&) {
    return "I'm just a program, but I'm running great. Thanks for asking!";
}

std::string handleBotName(const std::string&, ChatState&, const Config& cfg) {
    return "I'm " + cfg.botName + ".";
}

// ---- matchers ------------------------------------------------------------
bool isQuit(const std::string& s)      { return s == "quit" || s == "exit" || s == "bye"; }
bool isHelp(const std::string& s)      { return s == "help"; }
bool isHistory(const std::string& s)   { return s == "history"; }
bool isClear(const std::string& s)     { return s == "clear" || s == "reset"; }
bool isTime(const std::string& s)      { return s == "time"; }
bool isCalc(const std::string& s)      { return startsWith(s, "calc "); }
bool isSetName(const std::string& s)   { return startsWith(s, "my name is "); }
bool isGreeting(const std::string& s)  { return contains(s, "hello") || startsWith(s, "hi"); }
bool isHowAreYou(const std::string& s) { return contains(s, "how are you"); }
bool isBotName(const std::string& s)   { return contains(s, "your name"); }

// Order matters: first match wins.
const std::vector<Intent>& intents() {
    static const std::vector<Intent> table = {
        {isQuit, handleQuit, true},           {isHelp, handleHelp, true},
        {isHistory, handleHistory, true},     {isClear, handleClear, true},
        {isTime, handleTime, true},           {isCalc, handleCalc, true},
        {isSetName, handleSetName, false},    {isGreeting, handleGreeting, false},
        {isHowAreYou, handleHowAreYou, false}, {isBotName, handleBotName, false},
    };
    return table;
}

}  // namespace

std::string calculate(double a, char op, double b) {
    switch (op) {
        case '+': return "Result: " + std::to_string(a + b);
        case '-': return "Result: " + std::to_string(a - b);
        case '*': return "Result: " + std::to_string(a * b);
        case '/': return b == 0 ? "Cannot divide by zero." : "Result: " + std::to_string(a / b);
        default:  return "Unknown operator. Use + - * /";
    }
}

bool tryRuleReply(const std::string& input, ChatState& state, const Config& cfg,
                  bool commandsOnly, std::string& reply) {
    const std::string lower = toLower(input);
    for (const Intent& intent : intents()) {
        if (commandsOnly && !intent.isCommand) continue;
        if (intent.matches(lower)) {
            reply = intent.handle(input, state, cfg);
            return true;
        }
    }
    return false;
}

}  // namespace simplechat
