# Lesson 3 – Understanding and selecting Ollama models

## Reading a model name

```
qwen2.5-coder:7b-instruct-q4_K_M
└─ family ──┘ └size┘└─variant─┘└quant┘
```

- **Family**: who trained it and for what (`llama3.2`, `qwen2.5`, `gemma3`,
  `mistral`, `phi4-mini`, `deepseek-r1`, `qwen2.5-coder`, …).
- **Size**: number of parameters (`1b`, `3b`, `7b`, `14b`, `70b`). Bigger
  usually means smarter, but slower and hungrier for memory.
- **Variant**: `instruct`/chat models follow instructions. `text`/base models
  only continue text. For chat apps you want instruct (the default tag).
- **Quantization**: how many bits each weight is stored in. `q4_K_M` (about 4.5
  bits) is the usual default and a good balance. `q8_0` is closer to full
  quality at roughly twice the size. `fp16` is unquantized.
- No tag means `:latest`, which is the publisher's recommended default size
  and quant.

Browse everything at <https://ollama.com/library>.

## How big a model can I run?

Rule of thumb for 4-bit models: **memory needed ≈ 0.6 GB per billion
parameters + 1–2 GB** for the context and runtime.

| Size | Approx. memory (q4) | Runs well on |
|------|--------------------|--------------|
| 1B | ~1–2 GB | any laptop, CPU only |
| 3B–4B | ~3–4 GB | 8 GB RAM laptop, CPU is OK |
| 7B–8B | ~5–6 GB | 16 GB RAM, or a GPU with 8 GB VRAM |
| 12B–14B | ~9–11 GB | 32 GB RAM, or 12–16 GB VRAM |
| 27B–32B | ~18–22 GB | 24 GB VRAM, or a Mac with 32 GB+ |
| 70B | ~40+ GB | workstation or high-memory Mac |

If a model doesn't fit in VRAM, Ollama splits it between GPU and CPU, which
works but is much slower. `ollama ps` shows the split.

## Suggested models for this course

| Use | Try | Notes |
|-----|-----|-------|
| First test, weak hardware | `llama3.2:1b`, `gemma3:1b` | fast, basic quality |
| General chat (default in our code) | `llama3.2` (3B), `qwen2.5:3b` | good speed/quality balance |
| Better quality, 16 GB machines | `qwen2.5:7b`, `llama3.1:8b`, `gemma3:12b` | |
| Coding help | `qwen2.5-coder:7b` | |
| Step-by-step reasoning | `deepseek-r1:7b`, `qwen3` | emit `<think>…</think>` text before the answer |
| Embeddings (search/RAG) | `nomic-embed-text` | use `/api/embed`, not chat |

Pick a starting point, then compare: the C++ app takes `--model`, so the same
prompt can be tried against several models in a minute.

## The CLI

```bash
ollama pull llama3.2          # download (or update) a model
ollama run llama3.2           # interactive chat in the terminal (pulls if needed)
ollama run llama3.2 "Explain RAII in one sentence"   # one-shot prompt
ollama list                   # models on disk, with sizes
ollama ps                     # models loaded in memory, CPU/GPU split
ollama show llama3.2          # parameters, context length, template, license
ollama stop llama3.2          # unload from memory now
ollama rm llama3.2:1b         # delete from disk
ollama cp llama3.2 my-llama   # copy/rename
```

Inside `ollama run`: `/set parameter temperature 0.2`, `/show info`, `/bye`.

## The same through the API

```bash
curl http://localhost:11434/api/tags                  # == ollama list
```

```bash
curl http://localhost:11434/api/chat -d '{
  "model": "llama3.2",
  "messages": [{"role": "user", "content": "Why is the sky blue?"}],
  "stream": false,
  "options": {"temperature": 0.2, "num_ctx": 8192}
}'
```

Common `options`:

| Option | Meaning |
|--------|---------|
| `temperature` | randomness (0 = deterministic-ish, 1+ = creative) |
| `num_ctx` | context window in tokens. **Raise it** for long conversations or documents; the default is small and older messages get silently cut. |
| `num_predict` | max tokens to generate (OpenAI's `max_tokens`) |
| `top_p`, `top_k`, `repeat_penalty`, `seed`, `stop` | sampling controls |

A top-level `"keep_alive": "30m"` (or `-1` for forever) keeps the model
loaded between requests and avoids slow reloads.

## Customising a model with a Modelfile

```dockerfile
# Modelfile
FROM llama3.2
PARAMETER temperature 0.3
PARAMETER num_ctx 8192
SYSTEM """You are a patient C++ tutor. Answer with short code examples."""
```

```bash
ollama create cpp-tutor -f Modelfile
```

```bash
./build/llm_chat --model cpp-tutor
```

## Section 9 quiz – Working with Models

1. What do `7b` and `q4_K_M` mean in `qwen2.5:7b-q4_K_M`?
2. About how much memory does an 8B model at 4-bit need?
3. Which command shows whether a loaded model is on GPU or CPU?
4. Your chat "forgets" the start of a long conversation. Which option do you change?
5. How do you bake a system prompt into a reusable model?

<details><summary>Answers</summary>

1. 7 billion parameters, 4-bit "K-quant medium" quantization.
2. Roughly 5–6 GB.
3. `ollama ps`.
4. `num_ctx` (or `OLLAMA_CONTEXT_LENGTH` server-wide).
5. Write a Modelfile with `FROM` + `SYSTEM`, then `ollama create <name> -f Modelfile`.
</details>
