// Lightweight test runner (no external framework needed).

#include "ClaudeClient.hpp"
#include "Json.hpp"
#include "KnowledgeBase.hpp"
#include "RecipeEngine.hpp"

#include <functional>
#include <iostream>
#include <string>
#include <vector>

using namespace chef;

static int failures = 0;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::cerr << "  FAILED: " #cond " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            ++failures;                                                                \
        }                                                                              \
    } while (0)

static bool hasIngredient(const Recipe& r, const std::string& name) {
    for (const auto& l : r.lines)
        if (l.ingredient->name == name) return true;
    return false;
}

int main() {
    KnowledgeBase kb;
    std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"vegan dishes contain only vegan ingredients", [&] {
             for (unsigned seed = 0; seed < 50; ++seed) {
                 RecipeEngine e(kb, seed);
                 Preferences p;
                 p.vegan = true;
                 p.course = static_cast<Course>(seed % 3);
                 const Recipe r = e.generate(p);
                 for (const auto& l : r.lines) CHECK(l.ingredient->vegan);
             }
         }},
        {"gluten-free and nut-free are respected", [&] {
             for (unsigned seed = 0; seed < 50; ++seed) {
                 RecipeEngine e(kb, seed);
                 Preferences p;
                 p.glutenFree = true;
                 p.nutFree = true;
                 p.course = static_cast<Course>(seed % 3);
                 const Recipe r = e.generate(p);
                 for (const auto& l : r.lines) {
                     CHECK(l.ingredient->glutenFree);
                     CHECK(!l.ingredient->containsNuts);
                 }
             }
         }},
        {"mystery-box ingredients are always used", [&] {
             for (unsigned seed = 0; seed < 20; ++seed) {
                 RecipeEngine e(kb, seed);
                 Preferences p;
                 p.mustUse = {"salmon", "miso"};
                 const Recipe r = e.generate(p);
                 CHECK(hasIngredient(r, "salmon fillet"));
                 CHECK(hasIngredient(r, "white miso"));
                 CHECK(r.lines.front().ingredient->name == "salmon fillet");
             }
         }},
        {"avoided ingredients never appear", [&] {
             for (unsigned seed = 0; seed < 50; ++seed) {
                 RecipeEngine e(kb, seed);
                 Preferences p;
                 p.avoid = {"garlic", "dairy", "nuts"};
                 const Recipe r = e.generate(p);
                 for (const auto& l : r.lines) {
                     CHECK(l.ingredient->name != "garlic");
                     CHECK(l.ingredient->category != Category::Dairy);
                     CHECK(!l.ingredient->containsNuts);
                 }
             }
         }},
        {"desserts use only dessert-friendly ingredients", [&] {
             for (unsigned seed = 0; seed < 30; ++seed) {
                 RecipeEngine e(kb, seed);
                 Preferences p;
                 p.course = Course::Dessert;
                 const Recipe r = e.generate(p);
                 for (const auto& l : r.lines) CHECK(l.ingredient->dessert);
             }
         }},
        {"same seed gives the same recipe", [&] {
             Preferences p;
             p.cuisine = "thai";
             RecipeEngine a(kb, 42), b(kb, 42);
             CHECK(a.generate(p).name == b.generate(p).name);
         }},
        {"cuisine preference steers ingredient choice", [&] {
             int matches = 0, total = 0;
             for (unsigned seed = 0; seed < 30; ++seed) {
                 RecipeEngine e(kb, seed);
                 Preferences p;
                 p.cuisine = "japanese";
                 p.creativity = 0.1;
                 for (const auto& l : e.generate(p).lines) {
                     ++total;
                     for (const auto& c : l.ingredient->cuisines) matches += (c == "japanese");
                     matches += l.ingredient->cuisines.empty();
                 }
             }
             CHECK(matches * 10 >= total * 7);  // at least 70% on-cuisine at low creativity
         }},
        {"classic pairings score higher than clashing ones", [&] {
             RecipeEngine e(kb, 1);
             const double classic = e.pairingScore(*kb.find("cherry tomatoes"), *kb.find("basil"));
             const double clash = e.pairingScore(*kb.find("soy sauce"), *kb.find("fish sauce"));
             CHECK(classic > clash);
         }},
        {"recipes are complete and scored", [&] {
             RecipeEngine e(kb, 7);
             Preferences p;
             p.serves = 4;
             const Recipe r = e.generate(p);
             CHECK(!r.name.empty());
             CHECK(r.steps.size() >= 3);
             CHECK(!r.plating.empty());
             CHECK(r.judges.size() == 3);
             CHECK(r.kcalPerServing > 0);
             for (const auto& j : r.judges) CHECK(j.score >= 0 && j.score <= 10);
         }},
        {"unknown and conflicting mystery-box ingredients are rejected", [&] {
             RecipeEngine e(kb, 3);
             Preferences p;
             p.mustUse = {"unicorn steak"};
             bool threw = false;
             try { e.generate(p); } catch (const GenerationError&) { threw = true; }
             CHECK(threw);

             p.mustUse = {"pork belly"};
             p.vegetarian = true;
             threw = false;
             try { e.generate(p); } catch (const GenerationError&) { threw = true; }
             CHECK(threw);
         }},
        {"JSON round-trips escapes and unicode", [&] {
             const std::string original = "Crème \"brûlée\"\n\ttab \\ slash";
             const json::Value v = json::parse("{\"t\":" + json::quote(original) + "}");
             CHECK(v["t"].s == original);
             const json::Value u = json::parse(R"({"a":[1,true,null,"é🍳"]})");
             CHECK(u["a"].a.size() == 4);
             CHECK(u["a"].a[3].s == "\xC3\xA9\xF0\x9F\x8D\xB3");
         }},
        {"Claude prompt carries the user's constraints", [&] {
             RecipeEngine e(kb, 5);
             Preferences p;
             p.vegan = true;
             p.cuisine = "thai";
             p.avoid = {"peanuts"};
             const std::string prompt = chefUserPrompt(p, e.generate(p));
             CHECK(prompt.find("vegan") != std::string::npos);
             CHECK(prompt.find("thai") != std::string::npos);
             CHECK(prompt.find("peanuts") != std::string::npos);
         }},
    };

    for (const auto& [name, fn] : tests) {
        const int before = failures;
        fn();
        std::cout << (failures == before ? "[PASS] " : "[FAIL] ") << name << "\n";
    }
    std::cout << (failures ? "\nSome tests failed.\n" : "\nAll tests passed.\n");
    return failures ? 1 : 0;
}
