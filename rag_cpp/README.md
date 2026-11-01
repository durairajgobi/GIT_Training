# Project 5 – RAG System in C++

A small Retrieval-Augmented Generation (RAG) system written in C++17.

| Component        | Technology                                  |
|------------------|---------------------------------------------|
| Embeddings       | Ollama `nomic-embed-text` (`POST /api/embed`) |
| Vector store     | Qdrant in Docker (REST, cosine distance)    |
| Answer generation| Ollama `llama3.2` (`POST /api/chat`)        |
| HTTP / JSON      | libcurl + nlohmann/json                     |

```
            INGESTOR                                       QUERY
 files ─► chunk ─► embed ─► upsert ─┐        question ─► embed ─► search top-k
                                    ▼                                   │
                              ┌──────────┐ ◄────────────────────────────┘
                              │  Qdrant  │ ─► chunks ─► prompt ─► LLM ─► answer
                              └──────────┘
```

## Course map

| Lesson                                   | Where in the code |
|------------------------------------------|-------------------|
| What is an Embedding?                    | `include/rag/embedder.hpp`, `src/embedder.cpp` |
| Semantic Search / Cosine Similarity      | `include/rag/similarity.hpp`, `apps/similarity_demo.cpp` |
| The Vector Store                         | `include/rag/vector_store.hpp`, `src/vector_store.cpp` |
| Installing Docker & running the store    | `docker-compose.yml` |
| The Ingestor Application                 | `apps/ingestor.cpp`, `src/chunker.cpp` |
| The Query Application                    | `apps/query.cpp`, `src/llm.cpp` |

## 1. Prerequisites

* A C++17 compiler, CMake ≥ 3.16
* libcurl development headers. nlohmann/json is optional: CMake downloads it if it isn't installed.
* Docker (for Qdrant and Ollama). You can also run Ollama natively instead.

Ubuntu/Debian:

```bash
sudo apt install build-essential cmake libcurl4-openssl-dev nlohmann-json3-dev
```

macOS:

```bash
brew install cmake curl nlohmann-json
```

## 2. Start the vector store and models

```bash
docker compose up -d
```

```bash
docker compose exec ollama ollama pull nomic-embed-text
```

```bash
docker compose exec ollama ollama pull llama3.2
```

The Qdrant dashboard is at http://localhost:6333/dashboard.

## 3. Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build -j
```

```bash
ctest --test-dir build --output-on-failure
```

## 4. Run

Cosine similarity demo (only needs Ollama):

```bash
./build/similarity_demo "How do I keep my pet healthy?"
```

Ingest documents (files or directories; `--reset` recreates the collection):

```bash
./build/ingestor --reset data
```

Ask questions (one-shot, or with no arguments for interactive mode):

```bash
./build/query "What does cosine similarity measure?"
```

```bash
./build/query --show-context
```

## Configuration (environment variables)

| Variable            | Default                  |
|---------------------|--------------------------|
| `RAG_QDRANT_URL`    | `http://localhost:6333`  |
| `RAG_OLLAMA_URL`    | `http://localhost:11434` |
| `RAG_COLLECTION`    | `documents`              |
| `RAG_EMBED_MODEL`   | `nomic-embed-text`       |
| `RAG_CHAT_MODEL`    | `llama3.2`               |
| `RAG_CHUNK_WORDS`   | `200`                    |
| `RAG_CHUNK_OVERLAP` | `40`                     |
| `RAG_TOP_K`         | `4`                      |

If you change the embedding model, re-run the ingestor with `--reset`, because
the vector dimension must match the collection.

## Design notes

* **Chunking:** documents are split into overlapping word windows so a sentence
  cut at a boundary still appears whole in the neighbouring chunk.
* **Idempotent ingestion:** point IDs are an FNV-1a hash of `source#chunk`, so
  re-ingesting a file overwrites its points instead of duplicating them.
* **Grounded answers:** the system prompt tells the model to answer only from
  the retrieved context and to cite chunks like `[1]`.
