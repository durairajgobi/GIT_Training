#include "cost.hpp"

#include <map>

namespace claude {

std::optional<ModelPrice> lookup_price(const std::string& model) {
    // USD per 1M tokens: {input, output, cache read}.
    // List prices at time of writing - verify at https://claude.com/pricing
    static const std::map<std::string, ModelPrice> prices = {
        {"claude-fable-5-1",  {10.00, 50.00, 0.25}},
        {"claude-opus-5-5",   { 4.00, 20.00, 0.20}},
        {"claude-opus-5",     { 5.00, 25.00, 0.50}},
        {"claude-opus-4-8",   { 5.00, 25.00, 0.50}},
        {"claude-sonnet-5-5", { 2.00, 10.00, 0.20}},
        {"claude-haiku-5-5",  { 0.10,  0.50, 0.01}},  // prompts up to 100K tokens
    };
    // Longest known name that prefixes the model id ("claude-opus-5" must
    // not swallow "claude-opus-5-5").
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
    return (usage.input_tokens * price.input_per_million +
            usage.cache_creation_input_tokens * price.cache_write_per_million() +
            usage.cache_read_input_tokens * price.cache_read_per_million +
            usage.output_tokens * price.output_per_million) / 1'000'000.0;
}

}  // namespace claude
