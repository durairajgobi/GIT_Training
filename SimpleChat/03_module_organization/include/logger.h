// logger.h - Append-only transcript log.
#pragma once
#include <fstream>
#include <string>

namespace simplechat {

class Logger {
public:
    Logger() = default;
    // Opens `path` for appending; does nothing when `enabled` is false.
    void open(const std::string& path, bool enabled);
    void write(const std::string& who, const std::string& text);

private:
    std::ofstream out_;
};

}  // namespace simplechat
