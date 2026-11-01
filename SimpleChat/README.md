# SimpleChat: a function-based AI chatbot in C++

**Project 1: Refactoring SimpleChat.** You start with a working chatbot where everything is
crammed into `main()`, and refactor it in three phases into a configurable, testable,
modular program that can optionally use Claude as its "brain".

```
SimpleChat/
├── 00_original/                 The challenge: one big main(), magic values everywhere
├── 01_config_extraction/        Phase 1: settings pulled into a Config struct + file + env vars
├── 02_function_decomposition/   Phase 2: small single-purpose functions + intent table
└── 03_module_organization/      Phase 3: headers/sources per module, tests, CMake/Make,
                                          optional Claude API backend
```

Each folder builds and runs on its own, so you can diff one phase against the next:

```bash
diff 00_original/simple_chat.cpp 01_config_extraction/simple_chat.cpp
```

---

## Overview and challenge (`00_original`)

`simple_chat.cpp` is a rule-based bot that greets you, remembers your name, shows the
time, does `calc 2 * 21`, prints your history and quits. It works, but:

| Smell | Example |
|---|---|
| Magic values | `"You> "`, `"simplechat.log"`, `50`, `"SimpleBot"` hard-coded (some twice) |
| God function | Input, parsing, logic, logging and output all live in `main()` |
| Growing if/else chain | Every new feature is one more `else if` |
| Untestable | Nothing can be called without running the whole console loop |
| Duplicated logic | Trimming and lower-casing written inline |

**Challenge:** keep the behaviour the same and fix the structure.

```bash
cd 00_original && g++ -std=c++17 -Wall -Wextra -o simple_chat simple_chat.cpp && ./simple_chat
```

## Phase 1: configuration extraction (`01_config_extraction`)

* A single `Config` struct declares **every** setting with its default.
* Layered loading, lowest to highest priority: **defaults → `simplechat.conf` → `SIMPLECHAT_*` env vars**.
  Choose another file with `--config path`.
* The loop now reads `cfg.botPrompt` and so on, so you can rename or re-limit the bot without recompiling.
* Bad lines and unknown keys are reported with `file:line`. They don't crash the program.

```bash
cd 01_config_extraction && g++ -std=c++17 -Wall -Wextra -o simple_chat simple_chat.cpp
SIMPLECHAT_BOT_NAME=Ada ./simple_chat --config simplechat.conf
```

## Phase 2: function decomposition (`02_function_decomposition`)

* Reusable helpers: `trim`, `toLower`, `startsWith`, `contains`, `parseBool`, `formatNow`.
* All session data lives in `ChatState` and is passed explicitly. There are no globals.
* Every feature is a handler with the same signature
  `std::string handleX(const std::string&, ChatState&, const Config&)`.
* The `if/else` chain is now a **table of `{matcher, handler}`**. To add a feature you write one function and add one row.
* Pure logic such as `calculate(a, op, b)` is separated from I/O, so it can be tested.
* `main()` is down to 6 lines that read like a table of contents.

## Phase 3: module organization (`03_module_organization`)

| Module | Responsibility |
|---|---|
| `config.h/.cpp` | `Config` struct and layered loading |
| `text_utils.h/.cpp` | Pure string/time helpers |
| `logger.h/.cpp` | Timestamped transcript log |
| `chat_state.h/.cpp` | Session state and the history limit |
| `intents.h/.cpp` | Rule-based handlers and the intent table |
| `json.h/.cpp` | Tiny dependency-free JSON parse/dump |
| `claude_client.h/.cpp` | Optional Claude Messages API backend |
| `chat_app.h/.cpp` | Console front end: banner, input, routing, loop |
| `main.cpp` | Three lines: load config, run app |
| `tests/test_main.cpp` | Unit tests for the modules (no framework needed) |

Build and test with either tool:

```bash
cd 03_module_organization && make && make test && ./simple_chat
```

```bash
cd 03_module_organization && cmake -S . -B build && cmake --build build && ctest --test-dir build
```

### Turning on the AI backend (Claude)

The modular design pays off here: the AI backend is just one more module. With
`backend = claude`, local **commands** (`help`, `history`, `clear`, `time`, `calc`, `quit`) still
run offline, and everything else goes to Claude with the full conversation history.

```bash
export ANTHROPIC_API_KEY=sk-ant-...        # never put the key in simplechat.conf
```

```bash
SIMPLECHAT_BACKEND=claude ./simple_chat
```

Notes:

* C++ has no official Anthropic SDK, so `claude_client.cpp` makes raw HTTP calls with the `curl`
  command-line tool (it must be installed). The key goes into a private `0600` temp file
  that is deleted right after each request. It never appears on the command line or in the log.
* Defaults: model `claude-opus-5-5`, `max_tokens = 16000`, `effort = medium`. Lower `effort` makes replies faster and cheaper.
* `server_fallback = true` lets the API retry a refused request on a fallback model. Set it to `false` to turn that off.
* If the key is missing, the app warns you and falls back to the offline rules backend.
* `clear` wipes both the local history and the Claude conversation.

---

## Project 1 wrap-up

| | Before | After |
|---|---|---|
| Change the prompt or bot name | Edit and recompile | Edit `simplechat.conf` or set an env var |
| Add a feature | Insert into a 100-line `if/else` | One handler + one table row |
| Test logic | Impossible without a console | `make test` |
| Swap the "brain" | Rewrite `main()` | New module behind `ChatApp::reply()` |
| Compile units | 1 file | 8 modules + tests, built incrementally |

Key takeaways:

1. **Extract configuration first.** It's the cheapest refactor and it shows what the program actually depends on.
2. **Decompose by responsibility.** Once each function does one thing, the shape of the program becomes clear.
3. **Group functions into modules** with small headers. Dependencies point one way (`main → chat_app → intents/claude_client → config/text_utils`).
4. **Refactor in small, behaviour-preserving steps.** Every phase compiles and runs the same conversation.

**Ideas for practice:** add a `joke` intent, save and restore history to a file, or stream Claude's
replies token by token (`"stream": true` and parsing server-sent events).
