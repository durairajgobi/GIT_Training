// logger.cpp
#include "logger.h"

#include "text_utils.h"

namespace simplechat {

void Logger::open(const std::string& path, bool enabled) {
    if (enabled) out_.open(path, std::ios::app);
}

void Logger::write(const std::string& who, const std::string& text) {
    if (out_) out_ << "[" << formatNow("%Y-%m-%d %H:%M:%S") << "] " << who << ": " << text << "\n";
}

}  // namespace simplechat
