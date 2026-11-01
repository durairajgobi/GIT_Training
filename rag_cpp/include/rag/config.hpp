#pragma once
// Runtime settings, read from environment variables with sensible defaults.

#include <string>

namespace rag {

struct Config {
    std::string qdrant_url = "http://localhost:6333";
    std::string ollama_url = "http://localhost:11434";
    std::string collection = "documents";
    std::string embed_model = "nomic-embed-text";
    std::string chat_model = "llama3.2";
    std::size_t chunk_words = 200;    // words per chunk
    std::size_t chunk_overlap = 40;   // words shared between neighbouring chunks
    std::size_t top_k = 4;            // chunks retrieved per question

    // Overrides from RAG_QDRANT_URL, RAG_OLLAMA_URL, RAG_COLLECTION,
    // RAG_EMBED_MODEL, RAG_CHAT_MODEL, RAG_CHUNK_WORDS, RAG_CHUNK_OVERLAP, RAG_TOP_K.
    static Config from_env();
};

}  // namespace rag
