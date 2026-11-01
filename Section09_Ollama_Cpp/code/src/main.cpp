// Interactive chat that works with OpenAI or a local Ollama server.
//
//   llm_chat                         # Ollama, native API, model llama3.2
//   llm_chat --model qwen2.5:3b      # pick another local model
//   llm_chat --provider openai       # original OpenAI behaviour
//   llm_chat --list-models
//
// Environment: LLM_PROVIDER, LLM_MODEL, OLLAMA_HOST, OPENAI_API_KEY
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "http.hpp"
#include "llm_client.hpp"

namespace {

std::string envOr(const char* name, const std::string& fallback = "") {
    const char* v = std::getenv(name);
    return (v && *v) ? v : fallback;
}

// OLLAMA_HOST may be "127.0.0.1:11434" or "0.0.0.0" — normalise to a URL.
std::string normaliseHost(std::string h) {
    if (h.empty()) return h;
    if (h.rfind("http://", 0) != 0 && h.rfind("https://", 0) != 0) h = "http://" + h;
    if (h.find(':', h.find("//") + 2) == std::string::npos) h += ":11434";
    if (h.find("0.0.0.0") != std::string::npos)
        h.replace(h.find("0.0.0.0"), 7, "127.0.0.1");
    while (!h.empty() && h.back() == '/') h.pop_back();
    return h;
}

void printUsage() {
    std::cout <<
        "Usage: llm_chat [options]\n"
        "  --provider <openai|ollama|ollama-openai>  (default: ollama)\n"
        "  --model <name>          model to use (e.g. llama3.2, gpt-4o-mini)\n"
        "  --host <url>            server base URL (default per provider)\n"
        "  --system <text>         system prompt\n"
        "  --temperature <0..2>    sampling temperature (default 0.7)\n"
        "  --no-stream             wait for the full reply instead of streaming\n"
        "  --list-models           print available models and exit\n"
        "  --help\n"
        "In the chat: /clear resets history, /exit quits.\n";
}

}  // namespace

int main(int argc, char** argv) {
    llm::ClientConfig config;
    std::string providerName = envOr("LLM_PROVIDER", "ollama");
    config.model = envOr("LLM_MODEL");
    config.apiKey = envOr("OPENAI_API_KEY");
    std::string host;
    std::string systemPrompt = "You are a helpful assistant. Keep answers concise.";
    llm::ChatOptions options;
    bool stream = true, listOnly = false;

    try {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            auto next = [&]() -> std::string {
                if (i + 1 >= argc) throw std::invalid_argument(arg + " needs a value");
                return argv[++i];
            };
            if (arg == "--provider") providerName = next();
            else if (arg == "--model") config.model = next();
            else if (arg == "--host") host = next();
            else if (arg == "--system") systemPrompt = next();
            else if (arg == "--temperature") options.temperature = std::stod(next());
            else if (arg == "--no-stream") stream = false;
            else if (arg == "--list-models") listOnly = true;
            else if (arg == "--help" || arg == "-h") { printUsage(); return 0; }
            else throw std::invalid_argument("Unknown option " + arg);
        }

        config.provider = llm::parseProvider(providerName);
        if (host.empty() && config.provider != llm::Provider::OpenAI)
            host = normaliseHost(envOr("OLLAMA_HOST"));
        config.baseUrl = host;

        http::GlobalInit curlInit;
        auto client = llm::makeClient(config);

        if (listOnly) {
            for (const auto& name : client->listModels()) std::cout << name << '\n';
            return 0;
        }

        std::cout << "Connected to " << client->description() << "\n"
                  << "Type a message (/clear, /exit).\n";

        std::vector<llm::Message> history{{"system", systemPrompt}};
        std::string line;
        while (true) {
            std::cout << "\nyou> " << std::flush;
            if (!std::getline(std::cin, line) || line == "/exit") break;
            if (line.empty()) continue;
            if (line == "/clear") {
                history.resize(1);
                std::cout << "(history cleared)\n";
                continue;
            }

            history.push_back({"user", line});
            std::cout << "ai> " << std::flush;
            try {
                std::string reply;
                if (stream) {
                    reply = client->chatStream(history, [](const std::string& t) {
                        std::cout << t << std::flush;
                    }, options);
                } else {
                    reply = client->chat(history, options);
                    std::cout << reply;
                }
                std::cout << '\n';
                history.push_back({"assistant", reply});
            } catch (const std::exception& e) {
                history.pop_back();  // don't keep a question with no answer
                std::cerr << "\n[error] " << e.what() << '\n';
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
