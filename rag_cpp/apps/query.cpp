// Section 14 - The Query Application
//
// Retrieval-Augmented Generation:
//   1. embed the question
//   2. retrieve the top-k most similar chunks from Qdrant
//   3. put those chunks into the prompt as context
//   4. let the LLM answer using only that context
//
// Usage: query [--show-context] ["question"]   (no question -> interactive mode)

#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "rag/config.hpp"
#include "rag/embedder.hpp"
#include "rag/llm.hpp"
#include "rag/vector_store.hpp"

namespace {

const char* kSystemPrompt =
    "You are a helpful assistant. Answer the user's question using ONLY the "
    "information in the provided context. If the context does not contain the "
    "answer, say that you don't know. Cite sources in square brackets like [1].";

std::string build_prompt(const std::string& question, const std::vector<rag::SearchHit>& hits) {
    std::ostringstream p;
    p << "Context:\n";
    for (std::size_t i = 0; i < hits.size(); ++i) {
        p << "[" << i + 1 << "] (source: " << hits[i].payload.value("source", "?") << ")\n"
          << hits[i].payload.value("text", "") << "\n\n";
    }
    p << "Question: " << question << "\nAnswer:";
    return p.str();
}

void answer(const std::string& question, const rag::Config& cfg, rag::Embedder& embedder,
            rag::VectorStore& store, rag::Llm& llm, bool show_context) {
    const auto hits = store.search(embedder.embed(question), cfg.top_k);
    if (hits.empty()) {
        std::cout << "No documents found. Run the ingestor first.\n";
        return;
    }

    std::cout << "\nRetrieved chunks:\n" << std::fixed << std::setprecision(3);
    for (std::size_t i = 0; i < hits.size(); ++i) {
        std::cout << "  [" << i + 1 << "] score " << hits[i].score << "  "
                  << hits[i].payload.value("source", "?") << " #"
                  << hits[i].payload.value("chunk", 0) << '\n';
        if (show_context) std::cout << "      " << hits[i].payload.value("text", "") << "\n";
    }

    std::cout << "\nAnswer:\n" << llm.chat(kSystemPrompt, build_prompt(question, hits)) << "\n\n";
}

}  // namespace

int main(int argc, char** argv) {
    bool show_context = false;
    std::string question;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--show-context") show_context = true;
        else if (a == "-h" || a == "--help") {
            std::cout << "Usage: query [--show-context] [\"question\"]\n";
            return 0;
        } else {
            if (!question.empty()) question += ' ';
            question += a;
        }
    }

    try {
        const rag::Config cfg = rag::Config::from_env();
        rag::Embedder embedder(cfg.ollama_url, cfg.embed_model);
        rag::VectorStore store(cfg.qdrant_url, cfg.collection);
        rag::Llm llm(cfg.ollama_url, cfg.chat_model);

        if (!store.collection_exists()) {
            std::cerr << "Collection '" << cfg.collection << "' does not exist. Run the ingestor first.\n";
            return 1;
        }

        if (!question.empty()) {
            answer(question, cfg, embedder, store, llm, show_context);
            return 0;
        }

        std::cout << "RAG query (collection '" << cfg.collection << "', model '" << cfg.chat_model
                  << "'). Type 'exit' to quit.\n";
        for (std::string line;;) {
            std::cout << "> " << std::flush;
            if (!std::getline(std::cin, line) || line == "exit" || line == "quit") break;
            if (line.empty()) continue;
            try {
                answer(line, cfg, embedder, store, llm, show_context);
            } catch (const std::exception& e) {
                std::cerr << "Error: " << e.what() << '\n';
            }
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
