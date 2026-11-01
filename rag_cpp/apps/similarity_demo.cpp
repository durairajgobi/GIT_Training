// Section 14 - Semantic Search and Cosine Similarity
//
// Embeds a handful of sentences with Ollama and ranks them against a query by
// cosine similarity, entirely in memory. This is exactly what the vector store
// does for us later, just without the indexing.
//
// Usage: similarity_demo ["your query"]

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "rag/config.hpp"
#include "rag/embedder.hpp"
#include "rag/similarity.hpp"

int main(int argc, char** argv) {
    try {
        const rag::Config cfg = rag::Config::from_env();
        const std::string query = argc > 1 ? argv[1] : "How do I keep my pet healthy?";

        const std::vector<std::string> sentences = {
            "Dogs need regular exercise and a balanced diet.",
            "Take your cat to the vet once a year for a check-up.",
            "The stock market fell sharply on Tuesday.",
            "C++ templates allow generic programming at compile time.",
            "Fresh water should always be available for your puppy.",
            "The recipe calls for two cups of flour and an egg.",
        };

        rag::Embedder embedder(cfg.ollama_url, cfg.embed_model);
        const rag::Embedding q = embedder.embed(query);
        const auto vectors = embedder.embed_batch(sentences);

        std::cout << "Model: " << embedder.model() << " (dimension " << q.size() << ")\n";
        std::cout << "Query: \"" << query << "\"\n\n";

        std::vector<std::pair<double, std::string>> ranked;
        for (std::size_t i = 0; i < sentences.size(); ++i)
            ranked.emplace_back(rag::cosine_similarity(q, vectors[i]), sentences[i]);
        std::sort(ranked.begin(), ranked.end(),
                  [](const auto& a, const auto& b) { return a.first > b.first; });

        std::cout << std::fixed << std::setprecision(4);
        for (const auto& [score, text] : ranked) std::cout << "  " << score << "  " << text << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
