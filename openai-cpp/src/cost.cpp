#include "cost.hpp"

#include <map>

namespace openai {

std::optional<ModelPrice> lookup_price(const std::string& model) {
    // USD per 1M tokens: {input, cached input, output}.
    // Example values - verify at https://openai.com/api/pricing
    static const std::map<std::string, ModelPrice> prices = {
        {"gpt-4.1-nano", {0.10, 0.025, 0.40}},
        {"gpt-4o-mini",  {0.15, 0.075, 0.60}},
        {"gpt-4.1-mini", {0.40, 0.10,  1.60}},
        {"gpt-4.1",      {2.00, 0.50,  8.00}},
        {"gpt-4o",       {2.50, 1.25, 10.00}},
    };
    // The API often echoes a dated snapshot ("gpt-4o-mini-2024-07-18"),
    // so pick the longest known name that prefixes the model id.
    const ModelPrice* best = nullptr;
    size_t best_len = 0;
    for (const auto& [name, price] : prices) {
        if (model.compare(0, name.size(), name) == 0 && name.size() > best_len) {
            best = &price;
            best_len = name.size();
        }
    }
    if (!best) return std::nullopt;
    return *best;
}

double estimate_cost_usd(const Usage& usage, const ModelPrice& price) {
    const long uncached = usage.input_tokens - usage.cached_input_tokens;
    return (uncached * price.input_per_million +
            usage.cached_input_tokens * price.cached_input_per_million +
            usage.output_tokens * price.output_per_million) / 1'000'000.0;
}

long rough_token_estimate(const std::string& text) {
    return static_cast<long>((text.size() + 3) / 4);
}

}  // namespace openai
