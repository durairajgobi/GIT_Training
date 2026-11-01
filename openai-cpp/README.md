# Getting Started with OpenAI APIs using C++

Course notes plus a small working C++17 client for the OpenAI **Responses API**
(`POST /v1/responses`), built on **libcurl** (HTTPS) and **nlohmann/json** (JSON).

```
openai-cpp/
├── CMakeLists.txt
├── include/
│   ├── openai_request.hpp   # Building the OpenAI request
│   ├── https_client.hpp     # Making the HTTPS call
│   ├── openai_response.hpp  # Parsing & validating the response
│   └── cost.hpp             # Tokens, pricing, cost management
└── src/
    ├── main.cpp             # end-to-end demo
    ├── tests.cpp            # offline tests (no key / network needed)
    └── *.cpp
```

---

## 1. Introduction to OpenAI APIs and Cost Considerations

* The OpenAI API is a plain **HTTPS + JSON** web API. Any language that can
  send an HTTP POST can use it, so C++ works fine without an official SDK.
* It is **pay-as-you-go**: every call is billed by the number of **tokens** sent
  in and generated out. There is no free tier, so you add prepaid credit.
* Before coding, decide your cost guard rails:
  * Set a **monthly budget / usage limit** in the dashboard.
  * Cap output on every request with `max_output_tokens`.
  * Use the cheapest model that does the job (see section 6).
  * Log `usage` from every response so you always know what you spent.

## 2. OpenAI Account, API Key and Account Basics

1. Sign up at <https://platform.openai.com>.
2. **Billing** → add a payment method and some prepaid credit; set a usage limit.
3. **API keys** → *Create new secret key*. Copy it straight away; it is shown once.
4. Keep the key out of source code and out of git:

```bash
export OPENAI_API_KEY="sk-..."
```

The program reads it with `std::getenv("OPENAI_API_KEY")`. `.env` is in
`.gitignore`. If a key leaks, revoke it in the dashboard right away.

Useful dashboard pages: **Usage** (spend per day and per model), **Limits**
(rate limits and budget), and **Projects** (separate keys and budgets per app).

## 3. Section 8 – Getting Started with OpenAI

Install the toolchain and dependencies (Debian/Ubuntu):

```bash
sudo apt-get install -y g++ cmake libcurl4-openssl-dev nlohmann-json3-dev
```

Build, test and run:

```bash
cmake -S . -B build && cmake --build build
```

```bash
./build/openai_tests
```

```bash
./build/openai_demo "Explain RAII in one sentence" gpt-4o-mini
```

If nlohmann/json is not installed, CMake downloads it automatically (FetchContent).

## 4. Understanding the OpenAI API Structure

Every request has the same shape:

| Part            | Value                                         |
|-----------------|-----------------------------------------------|
| Base URL        | `https://api.openai.com/v1`                   |
| Endpoint        | `/responses` (also `/models`, `/embeddings`, …) |
| Method          | `POST`                                        |
| Auth header     | `Authorization: Bearer $OPENAI_API_KEY`       |
| Content type    | `Content-Type: application/json`              |
| Body            | JSON describing model + input                 |

Responses come back as JSON. Success is HTTP `200`. Errors use a 4xx or 5xx code
with a body like this:

```json
{ "error": { "message": "...", "type": "invalid_request_error", "code": "invalid_api_key" } }
```

| Status | Meaning                          | Retry?                 |
|--------|----------------------------------|------------------------|
| 400    | Bad request (invalid JSON or params) | No, fix the request |
| 401    | Missing or invalid API key       | No                     |
| 403    | Region, project or permission issue | No                  |
| 404    | Unknown model or endpoint        | No                     |
| 429    | Rate limit or quota exceeded     | Yes, with backoff (unless out of credit) |
| 5xx    | Server-side problem              | Yes, with backoff      |

## 5. Section 8 – The Responses API

`POST /v1/responses` is OpenAI's main text-generation endpoint.

**Minimal request**

```json
{
  "model": "gpt-4o-mini",
  "instructions": "You are a concise assistant for C++ developers.",
  "input": "Explain RAII in one sentence.",
  "max_output_tokens": 200
}
```

| Field               | Purpose                                                 |
|---------------------|---------------------------------------------------------|
| `model`             | Which model to run (this drives cost)                   |
| `input`             | A string, or an array of `{role, content}` messages     |
| `instructions`      | System-level guidance for the model                     |
| `max_output_tokens` | Hard cap on generated tokens, which also caps cost      |
| `temperature`       | 0–2; lower gives more predictable output                |
| `previous_response_id` | Continue a conversation without resending history    |

**Typical response (trimmed)**

```json
{
  "id": "resp_123",
  "object": "response",
  "status": "completed",
  "model": "gpt-4o-mini-2024-07-18",
  "output": [{
    "type": "message",
    "role": "assistant",
    "content": [{ "type": "output_text", "text": "RAII ties resource lifetime to object lifetime..." }]
  }],
  "usage": {
    "input_tokens": 32,
    "input_tokens_details": { "cached_tokens": 0 },
    "output_tokens": 18,
    "output_tokens_details": { "reasoning_tokens": 0 },
    "total_tokens": 50
  }
}
```

The answer text is at `output[] → type=="message" → content[] → type=="output_text" → text`.

## 6. Understanding Tokens, Pricing, and Cost Management

* A **token** is a piece of text, roughly 4 characters or ¾ of an English word.
  "Hello world!" is about 3 tokens.
* You pay for **input tokens** (instructions + input + any conversation history)
  and **output tokens** (the generated text, including hidden *reasoning tokens*
  on reasoning models).
* Output tokens usually cost **about 4× more** than input tokens.
* Repeated prompt prefixes can be billed at a cheaper **cached input** rate.
* Prices are quoted **per 1 million tokens**:

```
cost = (input - cached) × in_price/1M
     + cached           × cached_price/1M
     + output           × out_price/1M
```

Worked example for `gpt-4o-mini` ($0.15 in / $0.075 cached / $0.60 out per 1M),
with 1,000 input tokens (200 cached) and 500 output tokens:

```
800 × 0.15/1M + 200 × 0.075/1M + 500 × 0.60/1M = $0.000435
```

That is the exact case checked in `src/tests.cpp`.

## 7. Section 8 – Tokens and Costs

Ways to keep costs down:

1. **Measure:** read `usage` from every response and log it (`main.cpp` prints it).
2. **Cap:** always send `max_output_tokens`. If the cap is hit, `status` is
   `"incomplete"` with `incomplete_details.reason == "max_output_tokens"`.
3. **Trim input:** send short instructions and don't resend long history you don't need.
4. **Estimate before calling:** `rough_token_estimate()` uses the ~4 chars/token rule.
5. **Budget:** set dashboard usage limits and alerts.

## 8. Choosing Cost-Effective AI Models

Example list prices (USD per 1M tokens) used in `src/cost.cpp`.
**Prices change often, so check <https://openai.com/api/pricing> before relying on them.**

| Model          | Input | Cached input | Output | Good for                       |
|----------------|------:|-------------:|-------:|--------------------------------|
| `gpt-4.1-nano` | 0.10  | 0.025        | 0.40   | Classification, extraction, high volume |
| `gpt-4o-mini`  | 0.15  | 0.075        | 0.60   | Default for learning and general chat |
| `gpt-4.1-mini` | 0.40  | 0.10         | 1.60   | Better quality, still cheap   |
| `gpt-4.1`      | 2.00  | 0.50         | 8.00   | Harder coding and long-context tasks |
| `gpt-4o`       | 2.50  | 1.25         | 10.00  | High-quality multimodal       |

Rule of thumb: start with the smallest model, measure quality, and move up only
when the output isn't good enough. Keep the model name configurable (the demo
takes it as the second argument).

## 9. Building the OpenAI Request — `openai_request.cpp`

* `ResponseRequest` holds the model, input, optional instructions,
  `max_output_tokens` and temperature.
* `build_request_body()` **validates first** (empty input, temperature range,
  token cap) so bad requests fail locally and cost nothing.
* JSON is built with `nlohmann::json`, which escapes quotes, newlines and
  Unicode correctly. Never build JSON by concatenating strings.

```cpp
openai::ResponseRequest req;
req.input = "Explain RAII in one sentence.";
req.max_output_tokens = 200;
std::string body = openai::build_request_body(req).dump();
```

## 10. Making the HTTPS Call to OpenAI — `https_client.cpp`

libcurl steps:

1. `curl_global_init` once per program (the `CurlGlobal` RAII guard).
2. `curl_easy_init` creates a handle (wrapped in `unique_ptr` so it is always freed).
3. Set the URL, headers (`Content-Type`, `Authorization: Bearer …`), POST body,
   a write callback that appends to a `std::string`, and timeouts.
4. Leave TLS verification **on** (`CURLOPT_SSL_VERIFYPEER=1`, `VERIFYHOST=2`).
5. `curl_easy_perform`, then read the HTTP status with `CURLINFO_RESPONSE_CODE`.

Transport failures (DNS, TLS, timeout) throw `std::runtime_error`. HTTP 4xx and 5xx
responses are **returned**, not thrown, so the parser can read OpenAI's error JSON.

## 11. Parsing & Validating the OpenAI Response — `openai_response.cpp`

`parse_response(status, body)` checks the response in this order:

1. **Is it JSON?** A proxy or gateway can return HTML; this raises `invalid_response`.
2. **HTTP error?** Read `error.message/type/code` and throw `ApiError`.
3. **Error inside a 200?** Check `error` and `status == "failed"/"cancelled"`.
4. **Extract text** from every `output_text` part. A `refusal` part throws.
5. **Read usage** (input, cached, output, reasoning and total tokens) for cost tracking.
6. **Incomplete?** Return the partial text and set `incomplete_reason`.
7. **Empty?** If there is no text and no explanation, throw.

`ApiError::retryable()` returns true for 429 and 5xx, so callers can retry with
exponential backoff (for example 1s, 2s, 4s, 8s).

---

### Next steps

* Add retries with exponential backoff for retryable errors.
* Stream tokens as they arrive (`"stream": true`, parsed as server-sent events).
* Keep a running cost total per session and stop when it passes a budget.
