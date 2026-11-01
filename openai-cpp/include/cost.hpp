// Tokens, pricing and cost management
#pragma once
#include <optional>
#include <string>
#include "openai_response.hpp"

namespace openai {

// Prices in USD per 1 million tokens.
struct ModelPrice {
    double input_per_million;
    double cached_input_per_million;
    double output_per_million;
};

// Example list prices. ALWAYS confirm against https://openai.com/api/pricing
// before relying on them - prices change.
std::optional<ModelPrice> lookup_price(const std::string& model);

// Cost of one call in USD (cached input tokens are billed at the cached rate).
double estimate_cost_usd(const Usage& usage, const ModelPrice& price);

// Rough pre-flight estimate: ~4 characters of English per token.
long rough_token_estimate(const std::string& text);

}  // namespace openai
