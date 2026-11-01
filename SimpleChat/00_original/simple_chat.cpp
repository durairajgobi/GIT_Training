// SimpleChat - ORIGINAL (the "before" picture)
// ------------------------------------------------------------
// A working rule-based chatbot, written the way a lot of first
// versions get written: everything lives in main(), settings are
// hard-coded "magic values", and every feature is one more branch
// in an ever-growing if/else chain.
//
// It works, but it is hard to read, test, configure or extend.
// The three refactoring phases fix exactly those problems:
//   Phase 1 - Configuration Extraction
//   Phase 2 - Function Decomposition
//   Phase 3 - Module Organization
//
// Build: g++ -std=c++17 -Wall -Wextra -o simple_chat simple_chat.cpp
#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

int main() {
    std::vector<std::string> history;
    std::string userName;
    std::ofstream log("simplechat.log", std::ios::app);   // magic value

    std::cout << "==============================\n";
    std::cout << "  Welcome to SimpleChat v1.0\n";      // magic value
    std::cout << "==============================\n";
    std::cout << "Type 'help' for commands, 'quit' to exit.\n\n";

    while (true) {
        std::cout << "You> ";                           // magic value
        std::string line;
        if (!std::getline(std::cin, line)) break;

        // trim (inline, copy-pasted logic)
        size_t b = line.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) continue;
        size_t e = line.find_last_not_of(" \t\r\n");
        line = line.substr(b, e - b + 1);

        std::string lower = line;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        if (history.size() >= 50) history.erase(history.begin());  // magic value
        history.push_back(line);
        if (log) log << "USER: " << line << "\n";

        std::string reply;
        if (lower == "quit" || lower == "exit" || lower == "bye") {
            std::cout << "Bot> Goodbye" << (userName.empty() ? "" : ", " + userName) << "!\n";
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
            char buf[64];
            std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", std::localtime(&now));
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
            reply = "I'm SimpleBot.";                    // magic value, duplicated
        } else {
            reply = "I'm not sure how to respond to that. Type 'help' to see what I can do.";
        }

        std::cout << "Bot> " << reply << "\n";           // magic value, duplicated
        if (log) log << "BOT: " << reply << "\n";
    }
    return 0;
}
