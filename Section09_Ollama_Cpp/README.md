# Section 9 – Getting Started with Ollama and Local LLMs using C++

This section takes a C++ program that chats with the OpenAI API and makes it
run against a **local** model served by [Ollama](https://ollama.com), with no
API key, no per-token cost, and no data leaving your machine.

| # | Lesson | Notes |
|---|--------|-------|
| 1 | What is Ollama and why use it for learning? | [01_What_Is_Ollama.md](01_What_Is_Ollama.md) |
| 2 | Installing and configuring Ollama on Windows, macOS and Ubuntu Linux | [02_Installing_Ollama.md](02_Installing_Ollama.md) |
| 3 | Understanding and selecting Ollama models | [03_Working_With_Models.md](03_Working_With_Models.md) |
| 4 | Refactoring the OpenAI project to work with Ollama | [04_Refactoring_OpenAI_To_Ollama.md](04_Refactoring_OpenAI_To_Ollama.md) |

Each lesson ends with a short **Section 9 quiz** (answers included).

## The code

[`code/`](code/) is a small CMake project (C++17, libcurl, nlohmann/json):

```
code/
├── CMakeLists.txt
├── include/
│   ├── http.hpp            # RAII libcurl wrapper (GET/POST + streaming)
│   ├── llm_client.hpp      # provider-neutral LlmClient interface + factory
│   ├── openai_client.hpp   # OpenAI Chat Completions (also Ollama's /v1)
│   └── ollama_client.hpp   # Ollama native /api/chat, /api/tags
└── src/
    ├── http.cpp
    ├── openai_client.cpp
    ├── ollama_client.cpp
    ├── client_factory.cpp  # picks the client from --provider
    └── main.cpp            # interactive chat REPL
```

Quick start (after installing Ollama and the build tools from lesson 2):

```bash
ollama pull llama3.2
```

```bash
cd Section09_Ollama_Cpp/code
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

```bash
./build/llm_chat                               # local model, streamed
./build/llm_chat --model qwen2.5:3b            # another local model
./build/llm_chat --provider ollama-openai      # Ollama via OpenAI-compatible API
./build/llm_chat --provider openai             # original OpenAI (needs OPENAI_API_KEY)
./build/llm_chat --list-models
```

On Windows the executable is `build\Release\llm_chat.exe`.
