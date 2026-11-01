#include "rag/chunker.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace rag {

std::string normalize_whitespace(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    bool in_space = false;
    for (unsigned char ch : text) {
        if (std::isspace(ch)) {
            in_space = true;
        } else {
            if (in_space && !out.empty()) out.push_back(' ');
            in_space = false;
            out.push_back(static_cast<char>(ch));
        }
    }
    return out;
}

std::vector<Chunk> chunk_text(const std::string& text, std::size_t chunk_words, std::size_t overlap) {
    if (chunk_words == 0) throw std::invalid_argument("chunk_words must be > 0");
    if (overlap >= chunk_words) throw std::invalid_argument("overlap must be smaller than chunk_words");

    std::vector<std::string> words;
    std::istringstream in(text);
    for (std::string w; in >> w;) words.push_back(std::move(w));

    std::vector<Chunk> chunks;
    const std::size_t step = chunk_words - overlap;
    for (std::size_t start = 0; start < words.size(); start += step) {
        const std::size_t end = std::min(start + chunk_words, words.size());
        std::string piece;
        for (std::size_t i = start; i < end; ++i) {
            if (i > start) piece.push_back(' ');
            piece += words[i];
        }
        chunks.push_back({std::move(piece), chunks.size()});
        if (end == words.size()) break;
    }
    return chunks;
}

std::uint64_t stable_id(const std::string& source, std::size_t chunk_index) {
    const std::string key = source + "#" + std::to_string(chunk_index);
    std::uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : key) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open file: " + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

}  // namespace rag
