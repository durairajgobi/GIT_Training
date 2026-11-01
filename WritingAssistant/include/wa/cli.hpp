// Command-line parsing and the program's main loop, kept out of main() so it
// can be tested with injected streams and services.
#pragma once

#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

#include "wa/config.hpp"
#include "wa/feature.hpp"
#include "wa/service_container.hpp"

namespace wa {

struct CliArgs {
    std::string command;           // feature id, "list", "help" or "version"
    std::string text;              // positional text (joined with spaces)
    std::optional<std::string> file;
    FeatureOptions featureOptions; // --tone formal, --lang French, ...
    std::optional<std::string> provider, model, effort;
    bool noFallbacks = false;
    bool stats = false;            // print model and token usage to stderr
};

class CliError : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

CliArgs parseArgs(const std::vector<std::string>& argv);
void applyOverrides(const CliArgs& args, AppConfig& config);
std::string usage(const FeatureRegistry& features);

// Returns the process exit code.
int runCli(const std::vector<std::string>& argv, std::istream& in, std::ostream& out, std::ostream& err,
           const AppConfig& baseConfig, ServiceContainer& container);

}  // namespace wa
