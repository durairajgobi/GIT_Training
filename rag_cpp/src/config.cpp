#include "rag/config.hpp"

#include <cstdlib>
#include <stdexcept>

namespace rag {

namespace {

void env_str(const char* name, std::string& out) {
    if (const char* v = std::getenv(name); v && *v) out = v;
}

void env_size(const char* name, std::size_t& out) {
    if (const char* v = std::getenv(name); v && *v) {
        try {
            out = static_cast<std::size_t>(std::stoul(v));
        } catch (const std::exception&) {
            throw std::invalid_argument(std::string(name) + " must be a positive integer");
        }
    }
}

}  // namespace

Config Config::from_env() {
    Config c;
    env_str("RAG_QDRANT_URL", c.qdrant_url);
    env_str("RAG_OLLAMA_URL", c.ollama_url);
    env_str("RAG_COLLECTION", c.collection);
    env_str("RAG_EMBED_MODEL", c.embed_model);
    env_str("RAG_CHAT_MODEL", c.chat_model);
    env_size("RAG_CHUNK_WORDS", c.chunk_words);
    env_size("RAG_CHUNK_OVERLAP", c.chunk_overlap);
    env_size("RAG_TOP_K", c.top_k);
    return c;
}

}  // namespace rag
