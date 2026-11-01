#include "wa/cli.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>

#include "wa/http_client.hpp"
#include "wa/json.hpp"
#include "wa/writing_assistant.hpp"

namespace wa {

namespace {
constexpr const char* kVersion = "1.0.0";

std::string readAll(std::istream& in) {
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}
}  // namespace

CliArgs parseArgs(const std::vector<std::string>& argv) {
    CliArgs a;
    std::vector<std::string> positional;
    for (size_t i = 0; i < argv.size(); ++i) {
        std::string arg = argv[i];
        std::optional<std::string> inlineValue;  // from --key=value
        if (arg.rfind("--", 0) == 0) {
            if (auto eq = arg.find('='); eq != std::string::npos) {
                inlineValue = arg.substr(eq + 1);
                arg = arg.substr(0, eq);
            }
        }
        auto value = [&]() -> std::string {
            if (inlineValue) return *inlineValue;
            if (i + 1 >= argv.size()) throw CliError("option " + arg + " needs a value");
            return argv[++i];
        };
        auto flag = [&] {
            if (inlineValue) throw CliError("option " + arg + " does not take a value");
            return true;
        };

        if (arg == "-h" || arg == "--help") {
            a.command = "help";
            return a;
        } else if (arg == "--version") {
            a.command = "version";
            return a;
        } else if (arg == "--") {
            positional.insert(positional.end(), argv.begin() + i + 1, argv.end());
            break;
        } else if (arg == "-f" || arg == "--file") {
            a.file = value();
        } else if (arg == "--provider") {
            a.provider = value();
        } else if (arg == "--model") {
            a.model = value();
        } else if (arg == "--effort") {
            a.effort = value();
        } else if (arg == "--no-fallbacks") {
            a.noFallbacks = flag();
        } else if (arg == "--stats") {
            a.stats = flag();
        } else if (arg.rfind("--", 0) == 0 && arg.size() > 2) {
            a.featureOptions[arg.substr(2)] = value();
        } else {
            positional.push_back(arg);
        }
    }
    if (positional.empty()) throw CliError("missing command");
    a.command = positional.front();
    for (size_t i = 1; i < positional.size(); ++i) a.text += (i > 1 ? " " : "") + positional[i];
    return a;
}

void applyOverrides(const CliArgs& args, AppConfig& c) {
    if (args.provider) c.provider = *args.provider;
    if (args.model) c.model = *args.model;
    if (args.effort) c.effort = *args.effort;
    if (args.noFallbacks) c.serverSideFallbacks = false;
}

std::string usage(const FeatureRegistry& features) {
    std::ostringstream o;
    o << "AI Writing Assistant " << kVersion << "\n\n"
      << "Usage: writing_assistant <command> [options] [text...]\n"
      << "Text comes from the arguments, --file PATH, or standard input.\n\nCommands:\n";
    for (const auto& f : features.all()) {
        o << "  " << f->id() << std::string(12 - std::min<size_t>(11, f->id().size()), ' ') << f->description()
          << "\n";
        for (const auto& s : f->options()) {
            o << "      --" << s.name << "  " << s.description;
            if (!s.allowed.empty()) {
                o << " [";
                for (size_t i = 0; i < s.allowed.size(); ++i) o << (i ? "|" : "") << s.allowed[i];
                o << "]";
            }
            o << (s.defaultValue.empty() ? " (required)" : " (default: " + s.defaultValue + ")") << "\n";
        }
    }
    o << "  list        List features as JSON\n\n"
      << "Global options:\n"
      << "  --provider auto|anthropic|offline   default auto: anthropic if ANTHROPIC_API_KEY is set\n"
      << "  --model ID                          default claude-opus-5-5\n"
      << "  --effort low|medium|high|xhigh|max  default medium\n"
      << "  --no-fallbacks                      turn off server-side refusal fallbacks\n"
      << "  --stats                             print model and token usage to stderr\n"
      << "  -f, --file PATH                     read input from a file\n"
      << "  -h, --help, --version\n";
    return o.str();
}

int runCli(const std::vector<std::string>& argv, std::istream& in, std::ostream& out, std::ostream& err,
           const AppConfig& baseConfig, ServiceContainer& container) {
    CliArgs args;
    try {
        args = parseArgs(argv);
    } catch (const CliError& e) {
        ServiceContainer tmp;
        registerServices(tmp, baseConfig);
        err << "error: " << e.what() << "\n\n" << usage(*tmp.resolve<FeatureRegistry>());
        return 2;
    }

    AppConfig config = baseConfig;
    applyOverrides(args, config);

    try {
        registerServices(container, config);
        auto features = container.resolve<FeatureRegistry>();

        if (args.command == "help") { out << usage(*features); return 0; }
        if (args.command == "version") { out << kVersion << "\n"; return 0; }
        if (args.command == "list") {
            Json list = Json::array();
            for (const auto& f : features->all()) {
                Json j;
                j["id"] = f->id();
                j["description"] = f->description();
                Json opts = Json::array();
                for (const auto& s : f->options()) {
                    Json o;
                    o["name"] = s.name;
                    o["default"] = s.defaultValue;
                    Json allowed = Json::array();
                    for (const auto& a : s.allowed) allowed.push_back(a);
                    o["allowed"] = allowed;
                    opts.push_back(o);
                }
                j["options"] = opts;
                list.push_back(j);
            }
            out << list.dump() << "\n";
            return 0;
        }
        if (!features->find(args.command)) {
            err << "error: unknown command '" << args.command << "'\n\n" << usage(*features);
            return 2;
        }

        std::string text = args.text;
        if (args.file) {
            std::ifstream f(*args.file, std::ios::binary);
            if (!f) { err << "error: cannot read file '" << *args.file << "'\n"; return 2; }
            text = readAll(f);
        } else if (text.empty()) {
            text = readAll(in);
        }

        auto assistant = container.resolve<WritingAssistant>();
        CompletionResult r = assistant->run(args.command, text, args.featureOptions);
        out << r.text;
        if (r.text.empty() || r.text.back() != '\n') out << "\n";
        if (args.stats) {
            err << "[" << assistant->client().name() << "] model=" << r.model << " stop=" << r.stopReason
                << " input_tokens=" << r.inputTokens << " output_tokens=" << r.outputTokens << "\n";
        }
        return 0;
    } catch (const ValidationError& e) {
        err << "error: " << e.what() << "\n";
        return 2;
    } catch (const RefusalError& e) {
        err << "refused: " << e.what() << (e.category().empty() ? "" : " (category: " + e.category() + ")") << "\n";
        return 3;
    } catch (const LLMError& e) {
        err << "error: " << e.what() << "\n";
        return 4;
    } catch (const TransportError& e) {
        err << "network error: " << e.what() << "\n";
        return 4;
    } catch (const std::exception& e) {
        err << "error: " << e.what() << "\n";
        return 1;
    }
}

}  // namespace wa
