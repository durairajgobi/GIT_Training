#pragma once
// Splits documents into overlapping word windows small enough to embed well.

#include <cstdint>
#include <string>
#include <vector>

namespace rag {

struct Chunk {
    std::string text;
    std::size_t index = 0;
};

// Collapses all whitespace runs into single spaces and trims the ends.
std::string normalize_whitespace(const std::string& text);

// Sliding window of `chunk_words` words, each window sharing `overlap` words
// with the previous one. Throws if overlap >= chunk_words.
std::vector<Chunk> chunk_text(const std::string& text, std::size_t chunk_words, std::size_t overlap);

// Stable 64-bit id (FNV-1a) so re-ingesting the same file overwrites its points.
std::uint64_t stable_id(const std::string& source, std::size_t chunk_index);

std::string read_file(const std::string& path);

}  // namespace rag
