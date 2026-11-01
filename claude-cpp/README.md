# Getting Started with the Claude API using C++

Companion to [`../openai-cpp`](../openai-cpp): the same lessons, using Anthropic's
**Messages API** (`POST /v1/messages`). There is no official C++ SDK, so this
calls the REST API directly with **libcurl** (HTTPS) and **nlohmann/json** (JSON).

```
claude-cpp/
├── CMakeLists.txt
├── include/
│   ├── claude_request.hpp   # Building the Claude request
│   ├── https_client.hpp     # Making the HTTPS call
│   ├── claude_response.hpp  # Parsing & validating the response
│   └── cost.hpp             # Tokens, pricing, cost management
└── src/
    ├── main.cpp             # end-to-end demo
    ├── tests.cpp            # offline tests (no key / network needed)
    └── *.cpp
```

---

## 1. Introduction to the Claude API and Cost Considerations

* The Claude API is an **HTTPS + JSON** web API, so any language that can
  send an HTTP POST can use it.
* It is **pay-as-you-go**: you are billed per **token**, separately for input
  and output, at a rate that depends on the model.
* Cost guard rails to set up before you write code:
  * A **spend limit** on your organization or workspace in the Console.
  * A sensible `max_tokens` on every request (it is required).
  * An `effort` level that suits the task (see section 7).
  * Log `usage` from every response.

## 2. Anthropic Account, API Key and Account Basics

1. Sign up at <https://platform.claude.com>.
2. **Billing**: add credit and set spend limits.
3. **API keys**: create a key. Keys start with `sk-ant-`.
4. Keep the key out of source code and out of git:

```bash
export ANTHROPIC_API_KEY="sk-ant-..."
```

The program reads it with `std::getenv("ANTHROPIC_API_KEY")`. `.env` is in
`.gitignore`. **Workspaces** let you keep separate keys, limits and usage per app.

## 3. Getting Started

```bash
sudo apt-get install -y g++ cmake libcurl4-openssl-dev nlohmann-json3-dev
```

```bash
cmake -S . -B build && cmake --build build
```

```bash
./build/claude_tests
```

```bash
./build/claude_demo "Explain RAII in one sentence"
```

The demo also takes an optional model and effort level: `./build/claude_demo "<prompt>" claude-sonnet-5-5 medium`.

## 4. Understanding the Claude API Structure

| Part           | Value                                           |
|----------------|-------------------------------------------------|
| Base URL       | `https://api.anthropic.com/v1`                  |
| Endpoint       | `/messages` (also `/messages/count_tokens`, `/models`, …) |
| Method         | `POST`                                          |
| Auth header    | `x-api-key: $ANTHROPIC_API_KEY` (**not** `Authorization: Bearer`) |
| Version header | `anthropic-version: 2026-10-21` (required)      |
| Beta header    | `anthropic-beta: <feature>` (only for beta features) |
| Content type   | `content-type: application/json`                |

Errors come back as:

```json
{ "type": "error", "error": { "type": "rate_limit_error", "message": "..." } }
```

Every response also has a `request-id` header. Include it when you contact support.

| Status | `error.type`            | Retry?                |
|--------|-------------------------|-----------------------|
| 400    | `invalid_request_error` | No, fix the request   |
| 401    | `authentication_error`  | No                    |
| 402    | `billing_error`         | No                    |
| 403    | `permission_error`      | No                    |
| 404    | `not_found_error`       | No                    |
| 413    | `request_too_large`     | No                    |
| 429    | `rate_limit_error`      | Yes, with backoff     |
| 500    | `api_error`             | Yes, with backoff     |
| 529    | `overloaded_error`      | Yes, with backoff     |

## 5. The Messages API

**Request**

```json
{
  "model": "claude-opus-5-5",
  "max_tokens": 2000,
  "system": "You are a concise assistant for C++ developers.",
  "messages": [
    { "role": "user", "content": "Explain RAII in one sentence." }
  ],
  "output_config": { "effort": "low" },
  "fallbacks": "default"
}
```

| Field           | Purpose |
|-----------------|---------|
| `model`         | Which model runs the request (this drives cost) |
| `max_tokens`    | **Required.** Hard cap on output tokens, including thinking |
| `system`        | System prompt |
| `messages`      | The conversation: alternating `user` and `assistant` turns, ending with a `user` turn. To continue a chat, send the whole history again |
| `output_config.effort` | `low` / `medium` / `high` / `xhigh` / `max`: how hard the model thinks. On Claude Opus 5.5 the default is `medium` |
| `fallbacks`     | `"default"` re-runs a request that a safety classifier declined on a recommended fallback model. Needs the `anthropic-beta: server-side-fallback-2026-07-01` header |

Things that changed recently, so older tutorials may be wrong:
* Don't send `temperature`, `top_p` or `top_k`. API version `2026-10-21` rejects them.
* Claude Opus 5.5 always uses adaptive thinking. `thinking: {type: "disabled"}`
  and `budget_tokens` return a 400. Use `effort` to control thinking instead.
* The last message must come from the user. Assistant "prefill" returns a 400.

**Response (trimmed)**

```json
{
  "id": "msg_123",
  "type": "message",
  "model": "claude-opus-5-5",
  "content": [
    { "type": "thinking", "thinking": "", "signature": "..." },
    { "type": "text", "text": "RAII ties resource lifetime to object lifetime..." }
  ],
  "stop_reason": "end_turn",
  "stop_details": null,
  "usage": {
    "input_tokens": 32,
    "cache_creation_input_tokens": 0,
    "cache_read_input_tokens": 0,
    "output_tokens": 85
  }
}
```

The answer is the `content[]` blocks with `type == "text"`. Skip any other block type
(`thinking`, `fallback`, `tool_use`, …).

| `stop_reason`   | Meaning |
|-----------------|---------|
| `end_turn`      | Finished normally |
| `max_tokens`    | Cut off by your `max_tokens`. Raise it, or ask for a shorter answer |
| `stop_sequence` | Hit one of your `stop_sequences` |
| `tool_use`      | Wants to call a tool you defined |
| `refusal`       | Declined. This still returns **HTTP 200**, so check `stop_reason` before reading `content` |

## 6. Understanding Tokens, Pricing, and Cost Management

* A token is roughly 3–4 characters of English text. Don't guess: call
  `POST /v1/messages/count_tokens` for an exact input count before sending.
  It is free, and the demo does this.
* You pay for:
  * **input tokens**: system prompt plus the whole message history. The history
    is resent every turn, so long chats get more expensive per turn.
  * **output tokens**: the visible text **plus thinking tokens**. Thinking is
    billed even when the response shows it as an empty string.
* **Prompt caching** reuses a repeated prefix, such as a long system prompt:
  * cache **write** costs 1.25× the input price (5-minute TTL)
  * cache **read** costs about 0.05× the input price on Claude Opus 5.5
  * check `usage.cache_creation_input_tokens` and `usage.cache_read_input_tokens`
    to see whether caching is working.

```
cost = input_tokens                × in_price/1M
     + cache_creation_input_tokens × in_price×1.25/1M
     + cache_read_input_tokens     × cache_read_price/1M
     + output_tokens               × out_price/1M
```

Worked example for Claude Opus 5.5, with 1,000 uncached input tokens, a 2,000-token
cache write, a 10,000-token cache read and 500 output tokens:

```
1000×$4 + 2000×$5 + 10000×$0.20 + 500×$20  per 1M  =  $0.026
```

That is the exact case checked in `src/tests.cpp`.

## 7. Tokens and Costs in Practice

1. **Measure:** log `usage` from every response (`main.cpp` prints it).
2. **Count first:** use `/messages/count_tokens` before expensive calls.
3. **Tune effort:** `low` for chat, classification and simple Q&A; `high` or
   `xhigh` for hard coding and agent work. Lower effort means fewer thinking tokens.
4. **Set `max_tokens` deliberately:** too low cuts off answers (`stop_reason: "max_tokens"`)
   and you pay for a retry. About 16000 is a safe default for normal requests.
5. **Cache stable prefixes:** put fixed content first and changing content last.
6. **Batch API:** work that isn't urgent costs 50% less through `/v1/messages/batches`.

## 8. Choosing Cost-Effective Models

List prices in USD per 1M tokens (prices change, so check <https://claude.com/pricing>):

| Model                 | ID                  | Input | Output | Notes |
|-----------------------|---------------------|------:|-------:|-------|
| Claude Fable 5.1      | `claude-fable-5-1`  | 10.00 | 50.00  | Most capable; hardest, longest-running work |
| **Claude Opus 5.5**   | `claude-opus-5-5`   | 4.00  | 20.00  | **Default**: strong all-rounder |
| Claude Sonnet 5.5     | `claude-sonnet-5-5` | 2.00  | 10.00  | Fast and capable for everyday coding and agents |
| Claude Haiku 5.5      | `claude-haiku-5-5`  | 0.10  | 0.50   | Cheapest; high-volume, simple tasks (prompts ≤100K tokens; $0.50 / $2.50 beyond) |

All four have a 1M-token context window. Use the IDs exactly as shown, with no
date suffix. The Haiku cache-read price in `cost.cpp` ($0.01) is an estimate at
roughly 0.1× input, so confirm it on the pricing page.

Before switching to a smaller model, try the same model at **lower effort**. It
often keeps the quality while saving tokens. Claude Haiku 5.5 has no server-side
fallback, so the demo drops `fallbacks` for Haiku models.

## 9. Building the Request (`claude_request.cpp`)

* `MessageRequest` holds the model, `max_tokens`, system prompt, a
  `std::vector<Message>` conversation, optional effort and the fallback opt-in.
* `build_request_body()` **validates first**: there must be at least one message,
  roles must be `user` or `assistant`, the last turn must be `user`, and effort
  must be a known level. Bad requests fail locally and cost nothing.
* `request_headers()` adds the `anthropic-beta` header only when it is needed.
* `build_count_tokens_body()` builds the body for the free token count.

## 10. Making the HTTPS Call (`https_client.cpp`)

The libcurl flow is the same as in the OpenAI project, except for:
* the `x-api-key` and `anthropic-version` headers
* a header callback that captures `request-id`
* a 10-minute timeout, because long answers with lots of thinking can take minutes.
  For very long outputs, use streaming (`"stream": true`) instead.

Transport failures throw `std::runtime_error`. HTTP errors are returned for the parser.

## 11. Parsing & Validating the Response (`claude_response.cpp`)

`parse_response()` checks the response in this order:

1. **Is it JSON?** Otherwise it throws `invalid_response`.
2. **HTTP or API error?** Reads `error.type` and `error.message` and throws `ApiError`.
3. **Usage** is read first, so you can log cost even when the request fails.
   It also sets `served_by_fallback` if `usage.iterations` contains a `fallback_message`.
4. **Refusal?** `stop_reason == "refusal"` throws, with the `stop_details.category`.
5. **Text:** joins every `text` block and ignores `thinking` and `fallback` blocks.
6. **Truncated?** `max_tokens` responses are returned with `truncated() == true`.
   An empty answer with no explanation throws.

`ApiError::retryable()` returns true for 408, 409, 429 and 5xx (including 529), so
callers can retry with exponential backoff.

---

## OpenAI vs Claude at a glance

| | OpenAI Responses API | Claude Messages API |
|---|---|---|
| Endpoint | `POST /v1/responses` | `POST /v1/messages` |
| Auth | `Authorization: Bearer` | `x-api-key` + `anthropic-version` |
| Input | `input` (string or items) + `instructions` | `messages[]` + `system` |
| Output cap | `max_output_tokens` (optional) | `max_tokens` (**required**) |
| Answer text | `output[].content[].output_text` | `content[]` blocks with `type: "text"` |
| Finish signal | `status` / `incomplete_details` | `stop_reason` |
| Refusal | `refusal` content part | `stop_reason: "refusal"` (HTTP 200) |
| Token count | estimate locally | free `/messages/count_tokens` endpoint |
| Usage fields | `input_tokens`, `output_tokens`, `cached_tokens` | `input_tokens`, `output_tokens`, `cache_creation_input_tokens`, `cache_read_input_tokens` |

### Next steps

* Retries with exponential backoff (honour the `retry-after` header).
* Streaming (`"stream": true`, server-sent events) for long answers.
* Prompt caching with `cache_control` on a long system prompt.
* Tool use: define `tools` and loop while `stop_reason == "tool_use"`.
