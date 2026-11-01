#pragma once

#include "Ingredient.hpp"
#include "KnowledgeBase.hpp"

#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace chef {

struct RecipeLine {
    const Ingredient* ingredient;
    double grams;
    std::string role;   // e.g. "hero", "sauce", "garnish"
};

struct JudgeVerdict {
    std::string judge;
    double score;       // out of 10
    std::string comment;
};

struct Recipe {
    std::string name;
    std::string description;
    Course course = Course::Main;
    std::string cuisine;
    std::string technique;
    int serves = 2;
    int prepMinutes = 0;
    int cookMinutes = 0;
    std::vector<RecipeLine> lines;
    std::vector<std::string> steps;
    std::string plating;
    FlavorProfile profile{};
    double kcalPerServing = 0;
    double balanceScore = 0;      // 0..10
    double creativityScore = 0;   // 0..10
    double presentationScore = 0; // 0..10
    std::vector<JudgeVerdict> judges;
};

class GenerationError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Offline "AI chef": builds a dish slot by slot, choosing each ingredient by a
// learned-style score (cuisine fit + flavour pairing + dish balance) and
// sampling from a softmax whose temperature is controlled by "creativity".
class RecipeEngine {
public:
    RecipeEngine(const KnowledgeBase& kb, unsigned seed);

    Recipe generate(const Preferences& prefs);

    // Public for testing.
    double pairingScore(const Ingredient& a, const Ingredient& b) const;
    static double balanceScore(const FlavorProfile& p, Course course);
    bool allowed(const Ingredient& i, const Preferences& prefs) const;

private:
    struct Slot {
        std::string role;
        std::vector<Category> categories;
        bool optional;
    };

    std::vector<Slot> slotsFor(Course course) const;
    const Ingredient* pick(const Slot& slot, const Preferences& prefs,
                           const std::vector<RecipeLine>& chosen);
    double candidateScore(const Ingredient& cand, const Preferences& prefs,
                          const std::vector<RecipeLine>& chosen) const;
    static FlavorProfile profileOf(const std::vector<RecipeLine>& lines);

    void writeMethod(Recipe& r) const;
    void nameDish(Recipe& r);
    void judge(Recipe& r);

    const KnowledgeBase& kb_;
    std::mt19937 rng_;
};

} // namespace chef
