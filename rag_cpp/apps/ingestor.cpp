// Section 14 - The Ingestor Application
//
// Reads text documents, splits them into overlapping chunks, embeds every chunk
// with Ollama and stores (vector + text + metadata) in Qdrant.
//
// Usage: ingestor [--reset] <file-or-directory>...

#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "rag/chunker.hpp"
#include "rag/config.hpp"
#include "rag/embedder.hpp"
#include "rag/vector_store.hpp"

namespace fs = std::filesystem;

namespace {

constexpr std::size_t kBatchSize = 16;  // chunks embedded per Ollama request

const std::set<std::string> kExtensions = {".txt", ".md", ".markdown", ".rst", ".csv",
                                           ".cpp", ".hpp", ".h",  ".c",        ".py"};

void usage() {
    std::cerr << "Usage: ingestor [--reset] <file-or-directory>...\n"
                 "  --reset   delete the collection before ingesting\n";
}

std::vector<fs::path> collect_files(const std::vector<std::string>& inputs) {
    std::vector<fs::path> files;
    for (const auto& in : inputs) {
        const fs::path p(in);
        if (fs::is_directory(p)) {
            for (const auto& e : fs::recursive_directory_iterator(p)) {
                if (e.is_regular_file() && kExtensions.count(e.path().extension().string()))
                    files.push_back(e.path());
            }
        } else if (fs::is_regular_file(p)) {
            files.push_back(p);
        } else {
            std::cerr << "Skipping (not found): " << in << '\n';
        }
    }
    return files;
}

}  // namespace

int main(int argc, char** argv) {
    bool reset = false;
    std::vector<std::string> inputs;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--reset") reset = true;
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else inputs.push_back(a);
    }
    if (inputs.empty()) { usage(); return 1; }

    try {
        const rag::Config cfg = rag::Config::from_env();
        rag::Embedder embedder(cfg.ollama_url, cfg.embed_model);
        rag::VectorStore store(cfg.qdrant_url, cfg.collection);

        const auto files = collect_files(inputs);
        if (files.empty()) { std::cerr << "No files to ingest.\n"; return 1; }

        if (reset) {
            store.delete_collection();
            std::cout << "Deleted collection '" << cfg.collection << "'\n";
        }

        // Probe the model once so we know the vector dimension for the collection.
        const std::size_t dim = embedder.embed("dimension probe").size();
        if (store.ensure_collection(dim))
            std::cout << "Created collection '" << cfg.collection << "' (dim " << dim << ", cosine)\n";

        std::size_t total_chunks = 0;
        for (const auto& file : files) {
            const std::string source = file.generic_string();
            const std::string text = rag::normalize_whitespace(rag::read_file(source));
            const auto chunks = rag::chunk_text(text, cfg.chunk_words, cfg.chunk_overlap);
            if (chunks.empty()) continue;

            for (std::size_t b = 0; b < chunks.size(); b += kBatchSize) {
                const std::size_t e = std::min(b + kBatchSize, chunks.size());
                std::vector<std::string> texts;
                for (std::size_t i = b; i < e; ++i) texts.push_back(chunks[i].text);

                const auto vectors = embedder.embed_batch(texts);
                std::vector<rag::Point> points;
                for (std::size_t i = b; i < e; ++i) {
                    points.push_back({rag::stable_id(source, chunks[i].index), vectors[i - b],
                                      {{"text", chunks[i].text},
                                       {"source", source},
                                       {"chunk", chunks[i].index}}});
                }
                store.upsert(points);
            }
            total_chunks += chunks.size();
            std::cout << "Ingested " << source << " -> " << chunks.size() << " chunk(s)\n";
        }

        std::cout << "\nDone: " << files.size() << " file(s), " << total_chunks
                  << " chunk(s). Collection now holds " << store.count() << " point(s).\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
