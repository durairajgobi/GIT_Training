// End-to-end demo: build request -> count tokens -> HTTPS call ->
// parse/validate -> cost.
//
// Usage:
//   export ANTHROPIC_API_KEY="sk-ant-..."
//   ./claude_demo "Explain RAII in one sentence" [model] [effort]
#include <cstdlib>
#include <iomanip>
#include <iostream>

#include "claude_request.hpp"
#include "claude_response.hpp"
#include "cost.hpp"
#include "https_client.hpp"

int main(int argc, char* argv[]) {
    const char* key = std::getenv("ANTHROPIC_API_KEY");  // never hard-code keys
    if (!key || !*key) {
        std::cerr << "Set ANTHROPIC_API_KEY first.\n";
        return 1;
    }

    claude::MessageRequest req;
    req.system = "You are a concise assistant for C++ developers.";
    req.messages.push_back({"user", argc > 1 ? argv[1] : "Say hello from C++ in one short sentence."});
    if (argc > 2) req.model = argv[2];
    req.effort = argc > 3 ? argv[3] : "low";  // short answers don't need deep thinking
    req.max_tokens = 2000;
    // Haiku has no server-side fallback; don't send the parameter there.
    if (req.model.rfind("claude-haiku", 0) == 0) req.fallbacks_default = false;

    try {
        claude::CurlGlobal curl_init;

        // 1. Free pre-flight count of input tokens.
        const auto count = claude::post_json("/messages/count_tokens",
                                             claude::build_count_tokens_body(req).dump(), key);
        const long input_tokens = claude::parse_count_tokens(count.status_code, count.body);
        std::cerr << "input tokens (count_tokens): " << input_tokens << "\n";

        // 2. The real call.
        const auto http = claude::post_json("/messages", claude::build_request_body(req).dump(),
                                            key, claude::request_headers(req));
        const auto res = claude::parse_response(http.status_code, http.body);

        std::cout << res.text << "\n\n";
        if (res.truncated())
            std::cerr << "[warning] hit max_tokens - the answer is cut off\n";
        if (res.served_by_fallback)
            std::cerr << "[note] answered by fallback model " << res.model << "\n";

        std::cerr << "model=" << res.model << " id=" << res.id
                  << " stop_reason=" << res.stop_reason << "\n"
                  << "tokens: in=" << res.usage.input_tokens
                  << " cache_write=" << res.usage.cache_creation_input_tokens
                  << " cache_read=" << res.usage.cache_read_input_tokens
                  << " out=" << res.usage.output_tokens << "\n";

        if (auto price = claude::lookup_price(res.model)) {
            std::cerr << "estimated cost: $" << std::fixed << std::setprecision(6)
                      << claude::estimate_cost_usd(res.usage, *price) << "\n";
        } else {
            std::cerr << "no price entry for " << res.model << "\n";
        }
        return 0;
    } catch (const claude::ApiError& e) {
        std::cerr << "Claude API error (HTTP " << e.http_status << ", " << e.type
                  << "): " << e.what() << "\n";
        if (e.retryable()) std::cerr << "This error is retryable - back off and try again.\n";
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
    }
    return 1;
}
