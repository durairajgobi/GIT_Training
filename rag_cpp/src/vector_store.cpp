#include "rag/vector_store.hpp"

namespace rag {

VectorStore::VectorStore(std::string base_url, std::string collection)
    : base_(std::move(base_url)), collection_(std::move(collection)) {}

std::string VectorStore::collection_url() const { return base_ + "/collections/" + collection_; }

bool VectorStore::collection_exists() {
    const HttpResponse r = http_.request("GET", collection_url());
    if (r.status == 404) return false;
    if (!r.ok()) throw HttpError("Qdrant GET collection -> HTTP " + std::to_string(r.status) + ": " + r.body, r.status);
    return true;
}

bool VectorStore::ensure_collection(std::size_t dimension) {
    if (collection_exists()) {
        const json info = http_.get_json(collection_url());
        const auto existing = info["result"]["config"]["params"]["vectors"]["size"].get<std::size_t>();
        if (existing != dimension) {
            throw std::runtime_error("Collection '" + collection_ + "' has dimension " +
                                     std::to_string(existing) + " but the embedding model produces " +
                                     std::to_string(dimension) + ". Use --reset or another collection.");
        }
        return false;
    }
    http_.put_json(collection_url(),
                   {{"vectors", {{"size", dimension}, {"distance", "Cosine"}}}});
    return true;
}

void VectorStore::delete_collection() {
    const HttpResponse r = http_.request("DELETE", collection_url());
    if (!r.ok() && r.status != 404)
        throw HttpError("Qdrant DELETE collection -> HTTP " + std::to_string(r.status), r.status);
}

std::uint64_t VectorStore::count() {
    const json r = http_.post_json(collection_url() + "/points/count", {{"exact", true}});
    return r["result"]["count"].get<std::uint64_t>();
}

void VectorStore::upsert(const std::vector<Point>& points) {
    if (points.empty()) return;
    json arr = json::array();
    for (const auto& p : points) {
        arr.push_back({{"id", p.id}, {"vector", p.vector}, {"payload", p.payload}});
    }
    http_.put_json(collection_url() + "/points?wait=true", {{"points", arr}});
}

std::vector<SearchHit> VectorStore::search(const Embedding& query, std::size_t limit) {
    const json req = {{"vector", query}, {"limit", limit}, {"with_payload", true}};
    const json resp = http_.post_json(collection_url() + "/points/search", req);

    std::vector<SearchHit> hits;
    for (const auto& h : resp["result"]) {
        SearchHit hit;
        hit.id = h["id"].get<std::uint64_t>();
        hit.score = h["score"].get<double>();
        hit.payload = h.value("payload", json::object());
        hits.push_back(std::move(hit));
    }
    return hits;
}

}  // namespace rag
