# Lesson 4 – Refactoring the OpenAI project to work with Ollama

## Starting point

A typical "first OpenAI program" in C++ hard-codes everything in one place:

```cpp
// BEFORE: OpenAI baked into the app
const std::string url = "https://api.openai.com/v1/chat/completions";
const char* key = std::getenv("OPENAI_API_KEY");
json req = {{"model", "gpt-4o-mini"}, {"messages", history}};
headers.push_back("Authorization: Bearer " + std::string(key));
auto res = http_post(url, req.dump(), headers);
std::string reply = json::parse(res)["choices"][0]["message"]["content"];
```

There are two ways to make it talk to Ollama. The project in `code/`
supports both.

## Strategy A: point it at Ollama's OpenAI-compatible endpoint (5-minute change)

Ollama implements `/v1/chat/completions`, so only **configuration** changes:

| | OpenAI | Ollama (`/v1`) |
|-|--------|----------------|
| Base URL | `https://api.openai.com/v1` | `http://localhost:11434/v1` |
| API key | required | ignored (send any string, e.g. `ollama`) |
| Model | `gpt-4o-mini` | `llama3.2`, `qwen2.5:7b`, … (must be pulled) |
| Request/response JSON | identical | identical |

In our code this is `--provider ollama-openai`. Look at
`src/client_factory.cpp`: it builds the **same `OpenAiClient` class** with a
different base URL, key and model. Zero new request code.

Use this when you want one code path for cloud and local, or you use an
OpenAI SDK/library that lets you set the base URL.

## Strategy B: a native Ollama client behind an interface (the proper refactor)

The native API exposes Ollama-only features (`options.num_ctx`,
`keep_alive`, model management) and is the most complete. To support it
without scattering `if (provider == ...)` through the app:

1. **Extract an interface** with what the app needs, and nothing
   provider-specific (`include/llm_client.hpp`):
   ```cpp
   class LlmClient {
   public:
       virtual std::string chat(const std::vector<Message>&, const ChatOptions& = {}) = 0;
       virtual std::string chatStream(const std::vector<Message>&, const TokenHandler&,
                                      const ChatOptions& = {});
       virtual std::vector<std::string> listModels();
       virtual std::string description() const = 0;
   };
   ```
2. **Move the old OpenAI code** into `OpenAiClient : LlmClient`
   (`src/openai_client.cpp`). Behaviour unchanged.
3. **Move HTTP into its own module** (`src/http.cpp`) so neither client
   touches libcurl directly.
4. **Add `OllamaClient : LlmClient`** (`src/ollama_client.cpp`).
5. **Add a factory** (`makeClient`) that reads config (`--provider`,
   `LLM_PROVIDER`, `OLLAMA_HOST`, …) and returns a
   `std::unique_ptr<LlmClient>`.
6. **Make `main.cpp` depend only on `LlmClient`**. The chat loop no longer
   knows or cares which provider is behind it.

```
main.cpp ──▶ LlmClient (interface)
                ▲              ▲
         OpenAiClient     OllamaClient
         (/v1/chat/...)   (/api/chat)
                └──── http.cpp (libcurl) ────┘
```

### What actually differs between the two APIs

| Concern | OpenAI `/v1/chat/completions` | Ollama `/api/chat` |
|---------|------------------------------|--------------------|
| Auth | `Authorization: Bearer <key>` | none |
| Messages | `messages: [{role, content}]` | same |
| Temperature | top-level `temperature` | `options.temperature` |
| Max tokens | top-level `max_tokens` | `options.num_predict` |
| Streaming default | off (`stream: true` to enable) | **on** (send `stream: false` to disable) |
| Stream format | Server-Sent Events: `data: {...}` lines, ends with `data: [DONE]` | NDJSON: one JSON object per line, last has `"done": true` |
| Reply text | `choices[0].message.content` | `message.content` |
| Errors | `{"error": {"message": ...}}` | `{"error": "..."}` |
| List models | `GET /v1/models` → `data[].id` | `GET /api/tags` → `models[].name` |

Each of these rows is handled inside its client class, never in `main.cpp`.

### Streaming in C++: the one tricky part

libcurl hands you **arbitrary byte chunks**, not lines. A JSON line can be
split across two chunks, or one chunk can hold several lines. So
`OllamaClient::chatStream` keeps a buffer, cuts it at each `'\n'`, and only
parses complete lines:

```cpp
[&](const char* data, size_t len) {
    pending.append(data, len);
    size_t pos;
    while ((pos = pending.find('\n')) != std::string::npos) {
        handleLine(pending.substr(0, pos));   // parse one NDJSON object
        pending.erase(0, pos + 1);
    }
}
```

Streaming matters much more locally than in the cloud: on a laptop a reply
can take 10–30 s, and seeing tokens appear immediately makes the app usable.

### Gotchas found while refactoring

- **The model must be pulled first.** Otherwise Ollama returns HTTP 404
  `model "x" not found, try pulling it first`. The client surfaces that
  message instead of a generic failure.
- **First request is slow**: the model is loaded into memory. Later requests
  are fast until `keep_alive` expires.
- **Timeouts**: local generation is slower than the cloud. The HTTP layer
  uses a 10 min total timeout and a 10 s connect timeout.
- **`OLLAMA_HOST` formats vary** (`0.0.0.0`, `127.0.0.1:11434`,
  `http://host:port`). `main.cpp` normalises them to a URL.
- **C++ lifetime bug to avoid**: `for (auto& m : json::parse(s).at("models"))`
  iterates over a member of a temporary that has already been destroyed
  (undefined behaviour before C++23; in testing it silently printed nothing).
  Store the parsed `json` in a named variable first.
- **Smaller context**: send fewer or shorter history messages, or raise
  `num_ctx`.

## Try it

```bash
ollama pull llama3.2
```

```bash
cd code && cmake -S . -B build && cmake --build build
```

```bash
./build/llm_chat                                   # Strategy B, streamed
./build/llm_chat --provider ollama-openai          # Strategy A
OPENAI_API_KEY=sk-... ./build/llm_chat --provider openai --model gpt-4o-mini
./build/llm_chat --model qwen2.5:3b --temperature 0.2 --system "Answer in one sentence."
```

## Exercises

1. Add `num_ctx` to `ChatOptions` and send it from `OllamaClient` (the
   OpenAI client should ignore it).
2. Implement real streaming in `OpenAiClient` by parsing SSE `data:` lines.
   Then `--provider ollama-openai` streams too.
3. Add an `--embed "<text>"` mode that calls `POST /api/embed` with
   `nomic-embed-text` and prints the vector length.
4. Print tokens/second after each reply, using the `eval_count` and
   `eval_duration` fields in Ollama's final (`"done": true`) stream object.
