#include "ClaudeClient.hpp"

#include "Json.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace chef {

namespace {

// A private (0600) temporary file that is removed when it goes out of scope.
class TempFile {
public:
    explicit TempFile(const std::string& contents) {
        std::string tmpl = (std::filesystem::temp_directory_path() / "masterchef-XXXXXX").string();
        const int fd = mkstemp(tmpl.data());
        if (fd < 0) throw ClaudeError("could not create a temporary file");
        fchmod(fd, S_IRUSR | S_IWUSR);
        path_ = tmpl;
        size_t written = 0;
        while (written < contents.size()) {
            const ssize_t n = write(fd, contents.data() + written, contents.size() - written);
            if (n <= 0) { close(fd); throw ClaudeError("could not write a temporary file"); }
            written += static_cast<size_t>(n);
        }
        close(fd);
    }
    ~TempFile() { std::remove(path_.c_str()); }
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
    const std::string& path() const { return path_; }

private:
    std::string path_;
};

std::string shellQuote(const std::string& s) {
    std::string out = "'";
    for (char c : s) out += (c == '\'') ? std::string("'\\''") : std::string(1, c);
    return out + "'";
}

std::string getenvOr(const char* name, const std::string& fallback) {
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : fallback;
}

} // namespace

ClaudeClient::ClaudeClient()
    : apiKey_(getenvOr("ANTHROPIC_API_KEY", "")),
      baseUrl_(getenvOr("ANTHROPIC_BASE_URL", "https://api.anthropic.com")) {
    if (apiKey_.empty()) throw ClaudeError("ANTHROPIC_API_KEY is not set");
    while (!baseUrl_.empty() && baseUrl_.back() == '/') baseUrl_.pop_back();
}

bool ClaudeClient::available() {
    const char* key = std::getenv("ANTHROPIC_API_KEY");
    return key && *key;
}

std::string ClaudeClient::complete(const std::string& system, const std::string& user) const {
    // Request body. Opus 5.5 always thinks; effort is the depth control. Server-side
    // fallbacks retry on a recommended model if a safety classifier declines.
    std::ostringstream body;
    body << "{"
         << "\"model\":" << json::quote(model) << ","
         << "\"max_tokens\":" << maxTokens << ","
         << "\"thinking\":{\"type\":\"adaptive\"},"
         << "\"output_config\":{\"effort\":" << json::quote(effort) << "},"
         << "\"fallbacks\":\"default\","
         << "\"system\":" << json::quote(system) << ","
         << "\"messages\":[{\"role\":\"user\",\"content\":" << json::quote(user) << "}]"
         << "}";

    const TempFile bodyFile(body.str());
    const TempFile headerFile("x-api-key: " + apiKey_ + "\n"
                              "anthropic-version: 2026-10-21\n"
                              "anthropic-beta: server-side-fallback-2026-07-01\n"
                              "content-type: application/json\n");

    const std::string cmd = "curl -sS --max-time 600 -X POST " + shellQuote(baseUrl_ + "/v1/messages") +
                            " -H @" + shellQuote(headerFile.path()) +
                            " --data-binary @" + shellQuote(bodyFile.path()) +
                            " -w '\\n%{http_code}' 2>&1";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) throw ClaudeError("could not run curl (is it installed?)");
    std::string out;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, pipe)) > 0) out.append(buf, n);
    const int status = pclose(pipe);
    if (status != 0) throw ClaudeError("curl failed: " + out);

    const size_t nl = out.find_last_of('\n');
    const std::string httpCode = nl == std::string::npos ? "" : out.substr(nl + 1);
    const std::string payload = nl == std::string::npos ? out : out.substr(0, nl);

    json::Value resp;
    try {
        resp = json::parse(payload);
    } catch (const std::exception& e) {
        throw ClaudeError("HTTP " + httpCode + ": unreadable response (" + e.what() + ")");
    }

    if (httpCode != "200") {
        const std::string msg = resp["error"]["message"].isString() ? resp["error"]["message"].s : payload;
        throw ClaudeError("HTTP " + httpCode + ": " + msg);
    }

    const std::string stop = resp["stop_reason"].s;
    if (stop == "refusal") {
        const auto& cat = resp["stop_details"]["category"];
        throw ClaudeError("Claude declined this request" +
                          (cat.isString() ? " (category: " + cat.s + ")" : std::string()));
    }

    std::string text;
    for (const auto& block : resp["content"].a)
        if (block["type"].s == "text") text += block["text"].s;
    if (text.empty()) throw ClaudeError("Claude returned no text");
    if (stop == "max_tokens") text += "\n\n[Output truncated: max_tokens reached]";
    return text;
}

std::string chefSystemPrompt() {
    return "You are a MasterChef-calibre head chef and recipe developer. You write precise, "
           "cookable restaurant-quality recipes for home cooks. Respect every dietary restriction "
           "and avoided ingredient exactly. Use metric quantities (with imperial in brackets where "
           "helpful), realistic timings and temperatures, and explain the key technique that makes "
           "the dish work. Format the answer in plain text with these sections: a dish name, a "
           "one-line description, Serves / Prep / Cook times, Ingredients, Method (numbered), "
           "Plating, and a short 'Judges' notes' paragraph giving a taste, creativity and "
           "presentation score out of 10 with one sentence each.";
}

std::string chefUserPrompt(const Preferences& prefs, const Recipe& draft) {
    std::ostringstream p;
    p << "Create one original " << toLower(toString(prefs.course)) << " recipe for " << prefs.serves
      << (prefs.serves == 1 ? " person.\n" : " people.\n");
    if (!prefs.cuisine.empty()) p << "Cuisine: " << prefs.cuisine << ".\n";
    std::vector<std::string> diet;
    if (prefs.vegan) diet.push_back("vegan");
    if (prefs.vegetarian) diet.push_back("vegetarian");
    if (prefs.glutenFree) diet.push_back("gluten-free");
    if (prefs.nutFree) diet.push_back("nut-free");
    if (!diet.empty()) {
        p << "Dietary requirements:";
        for (const auto& d : diet) p << " " << d;
        p << ".\n";
    }
    if (!prefs.mustUse.empty()) {
        p << "Mystery-box ingredients that must feature:";
        for (const auto& m : prefs.mustUse) p << " " << m << ";";
        p << "\n";
    }
    if (!prefs.avoid.empty()) {
        p << "Do not use:";
        for (const auto& a : prefs.avoid) p << " " << a << ";";
        p << "\n";
    }
    p << "Creativity (0 = classic, 1 = experimental): " << prefs.creativity << ".\n\n";

    p << "My flavour-pairing model proposed this ingredient set as a starting point (technique: "
      << draft.technique << "). Use it as inspiration — keep what works, and swap or add "
         "ingredients if it makes a better dish:\n";
    for (const auto& l : draft.lines)
        p << "- " << l.ingredient->name << " (" << l.role << ")\n";
    return p.str();
}

} // namespace chef
