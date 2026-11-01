#include "rag/embedder.hpp"

#include <stdexcept>

namespace rag {

Embedder::Embedder(std::string ollama_url, std::string model)
    : url_(std::move(ollama_url)), model_(std::move(model)) {}

Embedding Embedder::embed(const std::string& text) {
    auto out = embed_batch({text});
    return std::move(out.front());
}

std::vector<Embedding> Embedder::embed_batch(const std::vector<std::string>& texts) {
    if (texts.empty()) return {};
    const json req = {{"model", model_}, {"input", texts}};
    const json resp = http_.post_json(url_ + "/api/embed", req);

    if (!resp.contains("embeddings") || !resp["embeddings"].is_array())
        throw std::runtime_error("Unexpected Ollama /api/embed response: " + resp.dump());

    auto vectors = resp["embeddings"].get<std::vector<Embedding>>();
    if (vectors.size() != texts.size())
        throw std::runtime_error("Ollama returned a different number of embeddings than inputs");
    return vectors;
}

}  // namespace rag
