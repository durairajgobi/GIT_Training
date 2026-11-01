# AI ChatBot – OOP Refactoring (C++)

Section 11 – Refactoring to Classes (Procedural → Object-Oriented), Project 2.

A terminal chatbot that talks to Claude through the Messages API, written as
three focused layers instead of one long procedural `main()`.

## Architecture

```
main.cpp            UI loop: reads input, prints replies, handles /commands
   │ uses
ChatClient          Conversation state + Claude API format (JSON, headers, errors)
   │ has-a (composition)
HttpClientHelper    libcurl wrapper: RAII handle, headers, POST, transport errors
```

| Phase | Class / file | OOP idea |
|-------|--------------|----------|
| 1 – Encapsulating the HTTP layer | `HttpClientHelper` (`include/HttpClientHelper.h`, `src/HttpClientHelper.cpp`) | Encapsulation and RAII. The `CURL*` handle and `curl_slist` are freed automatically. The class can be moved but not copied, and one-time global init uses a function-local static. No curl types appear in the header. |
| 2 – Composition and a clean interface | `ChatClient` (`include/ChatClient.h`, `src/ChatClient.cpp`) | Composition (it *has a* `HttpClientHelper`, it does not inherit from one). Public API: `sendMessage()`, `clearHistory()`, `turnCount()`. Request building and error parsing are private helpers. |
| 3 – Professional organization | `include/`, `src/`, `CMakeLists.txt`, `.gitignore` | Headers and sources are split, built with CMake, compiled with warnings on, and `main()` is kept thin. |

### API details handled by `ChatClient`

- Uses the model `claude-opus-5-5` (override it with `CHATBOT_MODEL`), adaptive thinking and `effort: medium`.
- Appends the full assistant `content` array to the history, so thinking blocks go back unchanged on the next turn.
- Enables server-side refusal fallback (`fallbacks: "default"`) and handles `stop_reason: "refusal"` and `"max_tokens"`.
- Turns HTTP errors into readable messages (`authentication_error - invalid x-api-key`) and rolls the history back when a request fails.

## Build

Requirements: a C++17 compiler, CMake 3.14 or newer, and libcurl development headers.
If nlohmann/json is not installed, CMake downloads it.

```bash
# Debian/Ubuntu
sudo apt install build-essential cmake libcurl4-openssl-dev nlohmann-json3-dev
```

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build
```

## Run

```bash
export ANTHROPIC_API_KEY="your-api-key"
```

```bash
./build/chatbot
```

Commands inside the chat: `/reset` clears the conversation, `/help` lists the commands, and `/quit` exits.
