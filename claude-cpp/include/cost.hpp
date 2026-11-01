// Tokens, pricing and cost management
#pragma once
#include <optional>
#include <string>
#include "claude_response.hpp"

namespace claude {

// Prices in USD per 1 million tokens.
struct ModelPrice {
    double input_per_million;
    double output_per_million;
    double cache_read_per_million;
    // Cache writes (5-minute TTL) cost 1.25x the base input price.
    double cache_write_per_million() const { return input_per_million * 1.25; }
};

// List prices for current models. Confirm at https://claude.com/pricing
// before relying on them - prices change.
std::optional<ModelPrice> lookup_price(const std::string& model);

// Cost of one call in USD.
double estimate_cost_usd(const Usage& usage, const ModelPrice& price);

}  // namespace claude
