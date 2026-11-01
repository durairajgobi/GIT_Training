#pragma once
// Thin client for the Qdrant vector database REST API (runs in Docker).

#include <cstdint>
#include <string>
#include <vector>

#include "rag/http_client.hpp"
#include "rag/similarity.hpp"

namespace rag {

struct Point {
    std::uint64_t id = 0;
    Embedding vector;
    json payload;  // e.g. {"text": ..., "source": ..., "chunk": ...}
};

struct SearchHit {
    std::uint64_t id = 0;
    double score = 0.0;  // cosine similarity
    json payload;
};

class VectorStore {
public:
    VectorStore(std::string base_url, std::string collection);

    bool collection_exists();
    // Creates the collection with cosine distance if missing; returns true if created.
    bool ensure_collection(std::size_t dimension);
    void delete_collection();
    std::uint64_t count();

    void upsert(const std::vector<Point>& points);
    std::vector<SearchHit> search(const Embedding& query, std::size_t limit);

private:
    std::string collection_url() const;
    HttpClient http_;
    std::string base_;
    std::string collection_;
};

}  // namespace rag
