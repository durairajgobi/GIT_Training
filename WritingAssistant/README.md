# AI Writing Assistant (C++)

A command-line writing assistant written in C++17 that uses Claude (Claude Opus 5.5 by
default) through the Claude Messages API. It has five features, uses dependency injection
throughout, has no third-party dependencies, and comes with a test suite and a GitHub
Actions workflow.

| Command     | What it does                                   | Options |
|-------------|------------------------------------------------|---------|
| `summarize` | Condense text to its key points                | `--length short\|medium\|long`, `--format paragraph\|bullets` |
| `proofread` | Fix grammar, spelling and punctuation          | `--dialect us\|uk`, `--explain yes\|no` |
| `tone`      | Rewrite text in a different tone               | `--tone formal\|casual\|friendly\|professional\|persuasive\|confident\|empathetic` (required) |
| `expand`    | Develop notes or a short draft into fuller prose | `--length short\|medium\|long`, `--audience "..."` |
| `translate` | Translate text into another language           | `--lang <language>` (required), `--formality neutral\|formal\|informal` |

## Build and test

Requirements: a C++17 compiler (GCC 9+ or Clang 10+), CMake 3.16+, and the `curl`
command-line tool (only needed for real API calls).

```bash
cd WritingAssistant
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Useful CMake options: `-DWA_SANITIZE=ON` (ASan + UBSan), `-DWA_WARNINGS_AS_ERRORS=ON`,
`-DWA_BUILD_TESTS=OFF`.

## Usage

```bash
export ANTHROPIC_API_KEY=sk-ant-...      # without a key the app runs in offline mode

./build/writing_assistant summarize --length short --file article.txt
echo "teh report is definately done" | ./build/writing_assistant proofread --explain yes
./build/writing_assistant tone --tone formal "hey, can u send the file asap"
./build/writing_assistant expand --audience "new engineers" "DI = pass deps in, easier tests"
./build/writing_assistant translate --lang Tamil "Good morning, how are you?"
./build/writing_assistant list           # features and options as JSON
./build/writing_assistant --help
```

Text comes from the positional arguments, `--file PATH`, or standard input.

Global options: `--provider auto|anthropic|offline`, `--model ID`, `--effort low|medium|high|xhigh|max`,
`--no-fallbacks`, and `--stats` (prints model and token usage to stderr).

Environment variables: `ANTHROPIC_API_KEY`, `ANTHROPIC_BASE_URL`, `WA_PROVIDER`, `WA_MODEL`,
`WA_EFFORT`, `WA_MAX_TOKENS`, `WA_FALLBACKS=0`.

Exit codes: `0` success, `2` bad usage or input, `3` the model declined the request,
`4` an API or network error, `1` anything else.

**Offline mode** (`--provider offline`, or no API key) uses a deterministic rule-based
client, so the demos and CI work without a network connection. Its output is a simple
heuristic, not AI.

## Architecture

```
main.cpp ─▶ runCli ─▶ registerServices (composition root, ServiceContainer)
                         │
                         ├─ IHttpClient   ◀── CurlHttpClient          (FakeHttpClient in tests)
                         ├─ ILLMClient    ◀── AnthropicClient(IHttpClient)
                         │                 ◀── OfflineClient          (RecordingLLMClient in tests)
                         ├─ FeatureRegistry ── Summarize / Proofread / Tone / Expand / Translate
                         └─ WritingAssistant(ILLMClient, FeatureRegistry)
```

**Phase 1: foundation and dependency injection**
- `ServiceContainer` (`include/wa/service_container.hpp`) is a small DI container with
  singleton, transient and instance registrations. It also detects circular dependencies.
  `registerServices` is the single place where concrete types are chosen. It leaves any
  existing registration alone, which is how tests swap in fakes.
- `IHttpClient` / `CurlHttpClient` handle HTTP transport. Headers, including the API key,
  go through a private `0600` temp config file, never on the command line.
- `ILLMClient` / `AnthropicClient` build the Messages API request
  (`anthropic-version: 2026-10-21`), skip thinking blocks, read token usage, retry
  408/409/429/5xx and network errors with exponential backoff, and raise `RefusalError`
  when `stop_reason` is `refusal`.
- `Json` is a minimal JSON parser and serializer, with full escaping and UTF-8 / surrogate
  pair handling.

**Phase 2: the five features**
- Each feature derives from `FeatureBase`, which validates the input (not empty, at most
  200k characters), rejects unknown or invalid options, and fills in defaults. A feature
  only describes its options and its prompt. The user's text is wrapped in `<text>` tags,
  and the system prompt says to treat it as content, never as instructions.
- To add a feature, write a class with `id()`, `options()` and `fill()`, then register it
  in `registerServices`.

**Phase 3: testing and workflows**
- `tests/` has 52 unit tests and uses a tiny built-in test framework (no GoogleTest
  download needed). They cover JSON, config, DI, every feature, the Anthropic client
  against a fake transport (request shape, errors, refusals, retries, backoff) and
  end-to-end CLI runs. CTest also runs smoke tests of the real binary.
- `.github/workflows/writing-assistant-ci.yml` builds with GCC and Clang, plus a
  sanitizer build, with warnings treated as errors. It runs all tests and demos all five
  features offline. If the repository has an `ANTHROPIC_API_KEY` secret, it also makes
  one live Claude call.

## API notes

- Default model: `claude-opus-5-5`. Effort defaults to `medium` (override with `--effort`).
  Claude Opus 5.5 always uses adaptive thinking, so the request sends no `thinking` field.
- `temperature`/`top_p`/`top_k` are never sent. API version `2026-10-21` rejects them.
- Server-side refusal fallbacks are on by default (`fallbacks: "default"` with the
  `server-side-fallback-2026-07-01` beta header). If a safety classifier declines, the
  API retries on a suitable fallback model. Turn this off with `--no-fallbacks` or
  `WA_FALLBACKS=0`.
