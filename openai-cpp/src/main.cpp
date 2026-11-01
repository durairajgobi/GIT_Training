// End-to-end demo: build request -> HTTPS call -> parse/validate -> cost.
//
// Usage:
//   export OPENAI_API_KEY="sk-..."
//   ./openai_demo "Explain RAII in one sentence" [model]
#include <cstdlib>
#include <iomanip>
#include <iostream>

#include "cost.hpp"
#include "https_client.hpp"
#include "openai_request.hpp"
#include "openai_response.hpp"

int main(int argc, char* argv[]) {
    const char* key = std::getenv("OPENAI_API_KEY");  // never hard-code keys
    if (!key || !*key) {
        std::cerr << "Set OPENAI_API_KEY first.\n";
        return 1;
    }

    openai::ResponseRequest req;
    req.input = argc > 1 ? argv[1] : "Say hello from C++ in one short sentence.";
    if (argc > 2) req.model = argv[2];
    req.instructions = "You are a concise assistant for C++ developers.";
    req.max_output_tokens = 200;  // caps the worst-case cost of this call

    try {
        openai::CurlGlobal curl_init;

        const auto body = openai::build_request_body(req);
        std::cerr << "~" << openai::rough_token_estimate(req.input)
                  << " input tokens (rough estimate)\n";

        const auto http = openai::post_json("/responses", body.dump(), key);
        const auto res = openai::parse_response(http.status_code, http.body);

        std::cout << res.text << "\n\n";
        if (!res.incomplete_reason.empty())
            std::cerr << "[warning] response incomplete: " << res.incomplete_reason << "\n";

        std::cerr << "model=" << res.model << " id=" << res.id << "\n"
                  << "tokens: in=" << res.usage.input_tokens
                  << " (cached " << res.usage.cached_input_tokens << ")"
                  << " out=" << res.usage.output_tokens
                  << " total=" << res.usage.total_tokens << "\n";

        if (auto price = openai::lookup_price(res.model)) {
            std::cerr << "estimated cost: $" << std::fixed << std::setprecision(6)
                      << openai::estimate_cost_usd(res.usage, *price) << "\n";
        } else {
            std::cerr << "no price entry for " << res.model << "\n";
        }
        return 0;
    } catch (const openai::ApiError& e) {
        std::cerr << "OpenAI error (HTTP " << e.http_status << ", " << e.type
                  << (e.code.empty() ? "" : ", " + e.code) << "): " << e.what() << "\n";
        if (e.retryable()) std::cerr << "This error is retryable - back off and try again.\n";
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
    }
    return 1;
}
