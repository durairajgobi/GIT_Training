#include "wa/offline_client.hpp"

#include <cctype>
#include <map>
#include <sstream>
#include <vector>

namespace wa {

namespace {

std::vector<std::string> splitSentences(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (size_t i = 0; i < text.size(); ++i) {
        cur += text[i];
        bool end = text[i] == '.' || text[i] == '!' || text[i] == '?';
        if (end && (i + 1 == text.size() || std::isspace(static_cast<unsigned char>(text[i + 1])))) {
            size_t s = cur.find_first_not_of(" \t\r\n");
            if (s != std::string::npos) out.push_back(cur.substr(s));
            cur.clear();
        }
    }
    size_t s = cur.find_first_not_of(" \t\r\n");
    if (s != std::string::npos) out.push_back(cur.substr(s));
    return out;
}

// Value of a "Key: value" line the features put into the user prompt.
std::string promptParam(const std::string& prompt, const std::string& key) {
    std::istringstream in(prompt);
    std::string line, prefix = key + ": ";
    while (std::getline(in, line)) {
        if (line.rfind(prefix, 0) == 0) return line.substr(prefix.size());
    }
    return "";
}

}  // namespace

std::string OfflineClient::summarize(const std::string& text, size_t maxSentences) {
    auto sentences = splitSentences(text);
    std::string out;
    for (size_t i = 0; i < sentences.size() && i < maxSentences; ++i) {
        if (!out.empty()) out += ' ';
        out += sentences[i];
    }
    return out;
}

std::string OfflineClient::proofread(const std::string& text) {
    static const std::map<std::string, std::string> fixes = {
        {"teh", "the"},         {"recieve", "receive"}, {"definately", "definitely"},
        {"seperate", "separate"}, {"occured", "occurred"}, {"untill", "until"},
        {"wich", "which"},      {"alot", "a lot"},      {"thier", "their"},
        {"i", "I"},             {"dont", "don't"},      {"cant", "can't"},
    };

    // Collapse runs of spaces and remove spaces before punctuation.
    std::string collapsed;
    for (char c : text) {
        if (c == ' ' && !collapsed.empty() && collapsed.back() == ' ') continue;
        if ((c == ',' || c == '.' || c == '!' || c == '?' || c == ';' || c == ':') && !collapsed.empty() &&
            collapsed.back() == ' ')
            collapsed.pop_back();
        collapsed += c;
    }

    // Word-level fixes and sentence capitalisation.
    std::string out, word;
    bool capitalizeNext = true;
    auto flush = [&] {
        if (word.empty()) return;
        std::string lower;
        for (char c : word) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        auto it = fixes.find(lower);
        std::string w = it != fixes.end() ? it->second : word;
        if (capitalizeNext) w[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(w[0])));
        capitalizeNext = false;
        out += w;
        word.clear();
    };
    for (char c : collapsed) {
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '\'') {
            word += c;
            continue;
        }
        flush();
        out += c;
        if (c == '.' || c == '!' || c == '?' || c == '\n') capitalizeNext = true;
    }
    flush();
    return out;
}

CompletionResult OfflineClient::complete(const CompletionRequest& request) {
    CompletionResult r;
    r.model = "offline";
    r.stopReason = "end_turn";
    const std::string& in = request.input;

    if (request.task == "summarize") {
        std::string length = promptParam(request.userPrompt, "Length");
        size_t n = length.rfind("short", 0) == 0 ? 1 : length.rfind("long", 0) == 0 ? 5 : 3;
        r.text = summarize(in, n);
    } else if (request.task == "proofread") {
        r.text = proofread(in);
    } else if (request.task == "tone") {
        r.text = "[" + promptParam(request.userPrompt, "Tone") + " tone] " + proofread(in);
    } else if (request.task == "expand") {
        r.text = proofread(in) +
                 "\n\n[offline mode: connect a Claude API key for real expansion with examples and detail]";
    } else if (request.task == "translate") {
        r.text = "[" + promptParam(request.userPrompt, "Target language") + "] " + in;
    } else {
        r.text = in;
    }
    return r;
}

}  // namespace wa
