# Lesson 1 – What is Ollama and why use it for learning?

## What it is

**Ollama** is a free, open-source tool that downloads, manages and runs
large language models (LLMs) on your own computer. It bundles:

- **A model runtime** built on `llama.cpp`, which runs quantized models
  efficiently on CPUs, and on GPUs (NVIDIA CUDA, AMD ROCm, Apple Metal)
  when available.
- **A model registry** (`ollama.com/library`) so getting a model is one
  command: `ollama pull llama3.2`.
- **A CLI** (`ollama run`, `list`, `ps`, `rm`, …) for chatting and managing
  models.
- **A local HTTP server** on `http://localhost:11434` with two APIs:
  - its **native REST API** (`/api/chat`, `/api/generate`, `/api/tags`, …)
  - an **OpenAI-compatible API** (`/v1/chat/completions`, `/v1/models`, …)

That last point is what makes it so easy to plug into a C++ program: your code
makes HTTP requests exactly as it would to a cloud provider, only to
`localhost`.

```
┌──────────────┐  HTTP/JSON   ┌──────────────────────────┐
│ your C++ app │ ───────────▶ │ ollama server :11434     │
│ (libcurl)    │ ◀─────────── │  └─ llama.cpp + model    │
└──────────────┘              │     weights on disk/GPU  │
                              └──────────────────────────┘
```

## Why use it for learning

| Benefit | Why it matters while learning |
|---------|-------------------------------|
| **Free** | No per-token billing. Run loops, retries and experiments without watching a bill. |
| **No API key / account** | Nothing to leak into git, nothing to rotate. |
| **Private** | Prompts and data never leave your machine, so you can use real documents. |
| **Offline** | Works on a plane, or behind a strict firewall. |
| **Model choice** | Swap between Llama, Qwen, Gemma, Mistral, Phi, DeepSeek… with one flag and compare. |
| **Transparency** | You can see memory use, load time and tokens/sec, which teaches what models actually cost to run. |
| **OpenAI-compatible** | What you learn transfers directly to cloud APIs, and vice-versa. |

## Trade-offs to be aware of

- **Smaller models are less capable** than frontier cloud models. A 3B model
  is great for learning the plumbing, but weaker at reasoning and long tasks.
- **Hardware bound**: speed depends on your RAM/VRAM and GPU. Without a GPU,
  stick to 1B–4B models.
- **Small default context window**: Ollama uses a context of only a few
  thousand tokens unless you raise `num_ctx` (see lesson 3).
- **You operate it**: updates, disk space (models are GBs) and the server
  process are your responsibility.

## Section 9 quiz – What Is Ollama and Why Use It

1. On which port does Ollama listen by default?
2. Name the two HTTP APIs Ollama exposes.
3. Give two reasons a local model is useful while learning.
4. What open-source inference engine does Ollama build on?

<details><summary>Answers</summary>

1. `11434` (`http://localhost:11434`).
2. The native REST API (`/api/...`) and the OpenAI-compatible API (`/v1/...`).
3. Any two of: free, no API key, private, offline, easy model switching.
4. `llama.cpp`.
</details>
