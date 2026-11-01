#pragma once
// Turns text into embeddings using a local Ollama server (POST /api/embed).

#include <string>
#include <vector>

#include "rag/http_client.hpp"
#include "rag/similarity.hpp"

namespace rag {

class Embedder {
public:
    Embedder(std::string ollama_url, std::string model);

    Embedding embed(const std::string& text);
    std::vector<Embedding> embed_batch(const std::vector<std::string>& texts);

    const std::string& model() const { return model_; }

private:
    HttpClient http_;
    std::string url_;
    std::string model_;
};

}  // namespace rag
