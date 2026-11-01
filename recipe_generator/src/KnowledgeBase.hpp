#pragma once

#include "Ingredient.hpp"

#include <string>
#include <vector>

namespace chef {

// The chef's "memory": ingredients, flavour profiles and classic pairings.
class KnowledgeBase {
public:
    KnowledgeBase();

    const std::vector<Ingredient>& ingredients() const { return ingredients_; }
    const Ingredient* find(const std::string& name) const;
    std::vector<std::string> cuisines() const;

    // Bonus in [0, 1] when two ingredients form a well-known classic pairing.
    double classicPairing(const std::string& a, const std::string& b) const;

private:
    std::vector<Ingredient> ingredients_;
    std::vector<std::pair<std::string, std::string>> classics_;
};

std::string toLower(std::string s);

} // namespace chef
