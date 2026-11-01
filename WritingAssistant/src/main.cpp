#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "wa/cli.hpp"

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    wa::AppConfig config = wa::AppConfig::fromEnvironment([](const char* n) { return std::getenv(n); });
    wa::ServiceContainer container;
    return wa::runCli(args, std::cin, std::cout, std::cerr, config, container);
}
