#include "KnowledgeBase.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace chef {

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string toString(Category c) {
    switch (c) {
        case Category::Protein:   return "Protein";
        case Category::Vegetable: return "Vegetable";
        case Category::Starch:    return "Starch";
        case Category::Aromatic:  return "Aromatic";
        case Category::Herb:      return "Herb";
        case Category::Spice:     return "Spice";
        case Category::Dairy:     return "Dairy";
        case Category::Sauce:     return "Sauce";
        case Category::Fruit:     return "Fruit";
        case Category::Nut:       return "Nut";
        case Category::Sweetener: return "Sweetener";
        case Category::Fat:       return "Fat";
    }
    return "Unknown";
}

std::string toString(Course c) {
    switch (c) {
        case Course::Starter: return "Starter";
        case Course::Main:    return "Main";
        case Course::Dessert: return "Dessert";
    }
    return "Main";
}

bool parseCourse(const std::string& s, Course& out) {
    const std::string v = toLower(s);
    if (v == "starter" || v == "appetizer" || v == "entree") { out = Course::Starter; return true; }
    if (v == "main" || v == "mains")                        { out = Course::Main;    return true; }
    if (v == "dessert" || v == "pudding" || v == "sweet")   { out = Course::Dessert; return true; }
    return false;
}

namespace {

using C = Category;
using Tags = std::vector<std::string>;

// Diet shorthands: vegan, vegetarian, gluten-free, contains nuts.
struct Diet { bool vegan, veg, gf, nuts; };
constexpr Diet kVegan{true, true, true, false};
constexpr Diet kVeg{false, true, true, false};
constexpr Diet kMeat{false, false, true, false};
constexpr Diet kVeganGluten{true, true, false, false};
constexpr Diet kVegGluten{false, true, false, false};
constexpr Diet kMeatGluten{false, false, false, false};
constexpr Diet kNut{true, true, true, true};

enum Use { Savory, Both, DessertOnly };

Ingredient make(std::string name, C cat, Tags cuisines, FlavorProfile f, Diet d,
                double kcal, double grams, std::string color,
                Tags techniques = {}, Use use = Savory) {
    Ingredient i;
    i.name = std::move(name);
    i.category = cat;
    i.cuisines = std::move(cuisines);
    i.flavor = f;
    i.vegan = d.vegan;
    i.vegetarian = d.veg;
    i.glutenFree = d.gf;
    i.containsNuts = d.nuts;
    i.kcalPer100g = kcal;
    i.gramsPerServing = grams;
    i.color = std::move(color);
    i.techniques = std::move(techniques);
    i.savory = use != DessertOnly;
    i.dessert = use != Savory;
    return i;
}

} // namespace

KnowledgeBase::KnowledgeBase() {
    //                                                 sweet salty sour bitter umami spicy rich arom
    ingredients_ = {
        // ---- Proteins -------------------------------------------------------------------
        make("chicken thigh", C::Protein, {}, {.1, .2, 0, 0, .6, 0, .5, .2}, kMeat, 209, 180, "golden",
             {"pan-seared", "roasted", "grilled", "braised"}),
        make("salmon fillet", C::Protein, {"japanese", "french", "american"}, {.1, .2, 0, 0, .6, 0, .8, .2}, kMeat, 208, 160, "pink",
             {"pan-seared", "roasted", "cured"}),
        make("king prawns", C::Protein, {"thai", "mediterranean", "chinese", "indian"}, {.3, .4, 0, 0, .7, 0, .3, .3}, kMeat, 99, 150, "coral",
             {"grilled", "stir-fried", "poached"}),
        make("lamb rack", C::Protein, {"mediterranean", "french", "indian"}, {.1, .2, 0, .1, .7, 0, .8, .4}, kMeat, 282, 200, "brown",
             {"roasted", "grilled"}),
        make("beef sirloin", C::Protein, {"american", "french", "japanese", "mexican"}, {.1, .2, 0, .1, .9, 0, .7, .2}, kMeat, 244, 200, "brown",
             {"pan-seared", "grilled", "seared"}),
        make("pork belly", C::Protein, {"chinese", "japanese", "american"}, {.2, .3, 0, 0, .7, 0, 1, .2}, kMeat, 518, 150, "golden",
             {"braised", "roasted"}),
        make("duck breast", C::Protein, {"french", "chinese"}, {.2, .2, 0, .1, .8, 0, .9, .3}, kMeat, 337, 180, "brown",
             {"pan-seared", "roasted"}),
        make("scallops", C::Protein, {"french", "japanese", "american"}, {.5, .3, 0, 0, .7, 0, .4, .2}, kMeat, 111, 120, "ivory",
             {"seared", "cured"}),
        make("firm tofu", C::Protein, {"japanese", "chinese", "thai"}, {0, .1, 0, 0, .3, 0, .3, .1}, kVegan, 144, 180, "ivory",
             {"crispy-fried", "glazed", "stir-fried"}),
        make("paneer", C::Protein, {"indian"}, {.1, .2, .1, 0, .3, 0, .7, .1}, kVeg, 296, 150, "ivory",
             {"tandoor-charred", "pan-seared"}),
        make("chickpeas", C::Protein, {"indian", "mediterranean"}, {.1, .1, 0, 0, .3, 0, .3, .1}, kVegan, 164, 150, "beige",
             {"roasted", "braised"}),
        make("burrata", C::Protein, {"italian"}, {.2, .2, .1, 0, .3, 0, .9, .1}, kVeg, 300, 100, "white",
             {"torn"}),
        make("cauliflower steak", C::Protein, {"mediterranean", "indian", "american"}, {.2, .1, 0, .2, .3, 0, .1, .2}, kVegan, 25, 250, "white",
             {"roasted", "charred"}),

        // ---- Vegetables -----------------------------------------------------------------
        make("cherry tomatoes", C::Vegetable, {"italian", "mediterranean", "mexican"}, {.5, .1, .5, 0, .6, 0, 0, .2}, kVegan, 18, 80, "red"),
        make("asparagus", C::Vegetable, {"french", "italian"}, {.2, 0, 0, .4, .2, 0, 0, .3}, kVegan, 20, 80, "green"),
        make("bok choy", C::Vegetable, {"chinese", "japanese", "thai"}, {.1, .1, 0, .3, .1, 0, 0, .1}, kVegan, 13, 90, "green"),
        make("shiitake mushrooms", C::Vegetable, {"japanese", "chinese"}, {.1, .1, 0, .1, .9, 0, .1, .4}, kVegan, 34, 70, "brown"),
        make("wild mushrooms", C::Vegetable, {"french", "italian"}, {.1, .1, 0, .1, .8, 0, .1, .5}, kVegan, 30, 80, "brown"),
        make("butternut squash", C::Vegetable, {"american", "italian", "mediterranean"}, {.7, 0, 0, 0, .2, 0, .2, .2}, kVegan, 45, 120, "orange"),
        make("charred corn", C::Vegetable, {"mexican", "american"}, {.7, .1, 0, .1, .2, 0, .1, .2}, kVegan, 86, 80, "yellow"),
        make("baby spinach", C::Vegetable, {"indian", "italian", "mediterranean"}, {0, .1, 0, .3, .2, 0, 0, .1}, kVegan, 23, 60, "green"),
        make("aubergine", C::Vegetable, {"mediterranean", "indian", "chinese"}, {.1, 0, 0, .3, .3, 0, .2, .1}, kVegan, 25, 120, "purple"),
        make("red peppers", C::Vegetable, {"mediterranean", "mexican", "chinese"}, {.6, 0, .1, .1, .1, .1, 0, .3}, kVegan, 31, 80, "red"),
        make("fennel", C::Vegetable, {"italian", "french", "mediterranean"}, {.3, 0, 0, .1, .1, 0, 0, .8}, kVegan, 31, 70, "white"),
        make("heritage carrots", C::Vegetable, {"french", "american"}, {.7, 0, 0, .1, .1, 0, 0, .2}, kVegan, 41, 90, "orange"),
        make("peas", C::Vegetable, {"french", "indian", "american"}, {.6, 0, 0, 0, .2, 0, 0, .2}, kVegan, 81, 70, "green"),
        make("beetroot", C::Vegetable, {"french", "american"}, {.7, 0, 0, .2, .1, 0, 0, .1}, kVegan, 43, 90, "purple"),

        // ---- Starches -------------------------------------------------------------------
        make("arborio rice", C::Starch, {"italian"}, {.1, 0, 0, 0, .1, 0, .2, 0}, kVegan, 350, 80, "white"),
        make("jasmine rice", C::Starch, {"thai", "chinese", "indian"}, {.1, 0, 0, 0, 0, 0, 0, .3}, kVegan, 360, 80, "white"),
        make("sushi rice", C::Starch, {"japanese"}, {.2, .1, .2, 0, .1, 0, 0, 0}, kVegan, 360, 80, "white"),
        make("fresh pappardelle", C::Starch, {"italian"}, {.1, .1, 0, 0, .1, 0, .3, 0}, kVegGluten, 288, 110, "yellow"),
        make("soba noodles", C::Starch, {"japanese"}, {.1, .1, 0, .2, .1, 0, 0, .1}, kVeganGluten, 336, 90, "brown"),
        make("rice noodles", C::Starch, {"thai", "chinese"}, {.1, 0, 0, 0, 0, 0, 0, 0}, kVegan, 360, 90, "white"),
        make("crushed new potatoes", C::Starch, {"french", "american", "mediterranean"}, {.1, .1, 0, 0, .1, 0, .3, 0}, kVegan, 77, 180, "golden"),
        make("pomme purée", C::Starch, {"french"}, {.1, .2, 0, 0, .1, 0, .9, 0}, kVeg, 180, 160, "ivory"),
        make("corn tortillas", C::Starch, {"mexican"}, {.2, .1, 0, 0, .1, 0, .1, .2}, kVegan, 218, 90, "yellow"),
        make("pearl couscous", C::Starch, {"mediterranean"}, {.1, 0, 0, 0, .1, 0, .1, 0}, kVeganGluten, 376, 80, "beige"),
        make("garlic naan", C::Starch, {"indian"}, {.1, .2, 0, 0, .1, 0, .4, .4}, kVegGluten, 310, 90, "golden"),

        // ---- Aromatics ------------------------------------------------------------------
        make("garlic", C::Aromatic, {}, {.1, 0, 0, .1, .3, .2, 0, 1}, kVegan, 149, 6, "white"),
        make("shallots", C::Aromatic, {"french", "thai"}, {.4, 0, 0, 0, .2, .1, 0, .7}, kVegan, 72, 25, "purple"),
        make("ginger", C::Aromatic, {"japanese", "chinese", "thai", "indian"}, {.1, 0, 0, .1, 0, .6, 0, .9}, kVegan, 80, 8, "golden"),
        make("lemongrass", C::Aromatic, {"thai"}, {.1, 0, .3, 0, 0, 0, 0, 1}, kVegan, 99, 8, "green"),
        make("spring onions", C::Aromatic, {"chinese", "japanese", "mexican"}, {.2, 0, 0, .1, .1, .2, 0, .7}, kVegan, 32, 15, "green"),
        make("red onion", C::Aromatic, {"mexican", "indian", "mediterranean", "american"}, {.4, 0, 0, 0, .1, .3, 0, .6}, kVegan, 40, 40, "purple"),

        // ---- Herbs ----------------------------------------------------------------------
        make("basil", C::Herb, {"italian", "thai"}, {.2, 0, 0, .1, 0, .1, 0, 1}, kVegan, 23, 4, "green", {}, Both),
        make("coriander", C::Herb, {"thai", "indian", "mexican", "chinese"}, {0, 0, .2, .1, 0, 0, 0, .9}, kVegan, 23, 4, "green"),
        make("thyme", C::Herb, {"french", "mediterranean", "american"}, {0, 0, 0, .2, 0, 0, 0, .9}, kVegan, 101, 2, "green", {}, Both),
        make("rosemary", C::Herb, {"italian", "mediterranean"}, {0, 0, 0, .3, 0, 0, 0, 1}, kVegan, 131, 2, "green"),
        make("mint", C::Herb, {"mediterranean", "indian", "thai"}, {.2, 0, 0, 0, 0, 0, 0, 1}, kVegan, 70, 3, "green", {}, Both),
        make("tarragon", C::Herb, {"french"}, {.2, 0, 0, .1, 0, 0, 0, .9}, kVegan, 295, 2, "green"),
        make("shiso", C::Herb, {"japanese"}, {.1, 0, .1, .2, 0, .1, 0, .9}, kVegan, 37, 2, "green"),

        // ---- Spices ---------------------------------------------------------------------
        make("smoked paprika", C::Spice, {"mediterranean", "mexican", "american"}, {.2, 0, 0, .1, .1, .3, 0, .7}, kVegan, 282, 2, "red"),
        make("garam masala", C::Spice, {"indian"}, {.1, 0, 0, .2, 0, .5, 0, 1}, kVegan, 379, 2, "brown"),
        make("cumin", C::Spice, {"indian", "mexican", "mediterranean"}, {0, 0, 0, .3, .1, .2, 0, .9}, kVegan, 375, 2, "brown"),
        make("chipotle", C::Spice, {"mexican", "american"}, {.1, 0, 0, .1, .1, .8, 0, .6}, kVegan, 282, 2, "red"),
        make("bird's eye chilli", C::Spice, {"thai", "chinese", "indian"}, {0, 0, 0, 0, 0, 1, 0, .3}, kVegan, 40, 2, "red"),
        make("sichuan pepper", C::Spice, {"chinese"}, {0, 0, .1, .1, 0, .8, 0, .9}, kVegan, 300, 1, "red"),
        make("star anise", C::Spice, {"chinese"}, {.4, 0, 0, .1, 0, .1, 0, 1}, kVegan, 337, 1, "brown"),
        make("cinnamon", C::Spice, {"indian", "mexican", "mediterranean"}, {.4, 0, 0, .1, 0, .2, 0, 1}, kVegan, 247, 1, "brown", {}, Both),
        make("vanilla bean", C::Spice, {"french"}, {.6, 0, 0, 0, 0, 0, .1, 1}, kVegan, 288, 1, "black", {}, DessertOnly),
        make("cardamom", C::Spice, {"indian"}, {.3, 0, 0, .1, 0, .2, 0, 1}, kVegan, 311, 1, "green", {}, Both),

        // ---- Dairy ----------------------------------------------------------------------
        make("parmesan", C::Dairy, {"italian"}, {.1, .8, .1, .1, 1, 0, .6, .2}, kVeg, 431, 15, "ivory"),
        make("crème fraîche", C::Dairy, {"french"}, {.1, .1, .4, 0, .1, 0, .9, 0}, kVeg, 393, 30, "white", {}, Both),
        make("greek yoghurt", C::Dairy, {"mediterranean", "indian"}, {.1, .1, .6, 0, .1, 0, .5, 0}, kVeg, 97, 50, "white", {}, Both),
        make("feta", C::Dairy, {"mediterranean"}, {.1, .8, .4, 0, .3, 0, .6, 0}, kVeg, 264, 25, "white"),
        make("double cream", C::Dairy, {"french", "american"}, {.3, 0, 0, 0, 0, 0, 1, 0}, kVeg, 467, 50, "white", {}, DessertOnly),
        make("mascarpone", C::Dairy, {"italian"}, {.3, 0, .1, 0, 0, 0, 1, 0}, kVeg, 429, 50, "white", {}, DessertOnly),
        make("coconut cream", C::Dairy, {"thai", "indian"}, {.4, 0, 0, 0, 0, 0, .9, .3}, kVegan, 330, 60, "white", {}, Both),

        // ---- Sauces & seasonings ---------------------------------------------------------
        make("white miso", C::Sauce, {"japanese"}, {.3, .8, .1, 0, 1, 0, .1, .2}, kVegan, 198, 15, "beige"),
        make("soy sauce", C::Sauce, {"japanese", "chinese"}, {.1, 1, 0, .1, .9, 0, 0, .1}, kVeganGluten, 53, 15, "brown"),
        make("fish sauce", C::Sauce, {"thai"}, {.1, 1, 0, 0, 1, 0, 0, .3}, kMeat, 35, 10, "amber"),
        make("tamarind", C::Sauce, {"thai", "indian"}, {.5, 0, .9, 0, .1, 0, 0, .2}, kVegan, 239, 15, "brown"),
        make("lime", C::Sauce, {"thai", "mexican", "indian"}, {.1, 0, 1, .1, 0, 0, 0, .6}, kVegan, 30, 20, "green", {}, Both),
        make("lemon", C::Sauce, {"mediterranean", "italian", "french"}, {.1, 0, 1, .1, 0, 0, 0, .6}, kVegan, 29, 20, "yellow", {}, Both),
        make("aged balsamic", C::Sauce, {"italian"}, {.6, 0, .7, 0, .2, 0, 0, .3}, kVegan, 88, 10, "black", {}, Both),
        make("red wine jus", C::Sauce, {"french"}, {.2, .3, .3, .1, .7, 0, .2, .3}, kMeatGluten, 60, 40, "burgundy"),
        make("salsa verde", C::Sauce, {"italian", "mexican"}, {.1, .3, .5, .1, .1, .2, .3, .8}, kVegan, 120, 25, "green"),
        make("tahini", C::Sauce, {"mediterranean"}, {.1, .1, 0, .4, .2, 0, .8, .2}, kVegan, 595, 20, "beige"),
        make("gochujang", C::Sauce, {"japanese", "chinese"}, {.4, .5, 0, 0, .6, .8, 0, .2}, kVeganGluten, 240, 15, "red"),

        // ---- Fats -----------------------------------------------------------------------
        make("brown butter", C::Fat, {"french", "italian", "american"}, {.2, .1, 0, 0, .1, 0, 1, .5}, kVeg, 717, 15, "golden", {}, Both),
        make("extra virgin olive oil", C::Fat, {"italian", "mediterranean"}, {0, 0, 0, .3, 0, .1, .9, .4}, kVegan, 884, 15, "green"),
        make("toasted sesame oil", C::Fat, {"japanese", "chinese"}, {0, 0, 0, .1, .2, 0, .9, .8}, kVegan, 884, 6, "amber"),
        make("ghee", C::Fat, {"indian"}, {.1, 0, 0, 0, .1, 0, 1, .4}, kVeg, 900, 12, "golden"),

        // ---- Fruit ----------------------------------------------------------------------
        make("mango", C::Fruit, {"thai", "indian", "mexican"}, {.9, 0, .3, 0, 0, 0, 0, .5}, kVegan, 60, 90, "orange", {"fresh"}, Both),
        make("pomegranate", C::Fruit, {"mediterranean", "indian"}, {.6, 0, .5, .1, 0, 0, 0, .2}, kVegan, 83, 40, "red", {"fresh"}, Both),
        make("figs", C::Fruit, {"mediterranean", "italian", "french"}, {.8, 0, .1, 0, .1, 0, .1, .3}, kVegan, 74, 70, "purple", {"roasted"}, Both),
        make("strawberries", C::Fruit, {"french", "american", "italian"}, {.8, 0, .4, 0, 0, 0, 0, .5}, kVegan, 32, 100, "red", {"macerated"}, DessertOnly),
        make("poached pear", C::Fruit, {"french"}, {.8, 0, .2, 0, 0, 0, 0, .4}, kVegan, 57, 120, "golden", {"poached"}, DessertOnly),
        make("dark chocolate", C::Fruit, {"french", "mexican", "american"}, {.4, 0, 0, .7, 0, 0, .7, .4}, kVeg, 546, 50, "brown", {"tempered"}, DessertOnly),
        make("yuzu", C::Fruit, {"japanese"}, {.3, 0, .9, .2, 0, 0, 0, 1}, kVegan, 21, 20, "yellow", {"curd"}, Both),
        make("blood orange", C::Fruit, {"italian", "mediterranean"}, {.6, 0, .6, .1, 0, 0, 0, .6}, kVegan, 47, 80, "red", {"segmented"}, Both),

        // ---- Nuts & crunch --------------------------------------------------------------
        make("toasted pistachios", C::Nut, {"mediterranean", "indian", "italian"}, {.3, .1, 0, .1, .2, 0, .7, .3}, kNut, 562, 15, "green", {}, Both),
        make("candied pecans", C::Nut, {"american", "french"}, {.8, .1, 0, .1, .1, 0, .7, .3}, kNut, 690, 15, "brown", {}, Both),
        make("roasted peanuts", C::Nut, {"thai", "chinese", "american"}, {.1, .4, 0, .1, .4, 0, .7, .2}, kNut, 585, 15, "golden"),
        make("toasted sesame seeds", C::Nut, {"japanese", "chinese"}, {.1, 0, 0, .2, .2, 0, .6, .4}, kVegan, 573, 5, "ivory", {}, Both),
        make("dukkah", C::Nut, {"mediterranean"}, {.1, .3, 0, .2, .2, .2, .5, .8}, kNut, 520, 10, "brown"),

        // ---- Sweeteners -----------------------------------------------------------------
        make("honey", C::Sweetener, {"mediterranean", "french", "american"}, {1, 0, 0, 0, 0, 0, 0, .4}, kVeg, 304, 15, "amber", {}, Both),
        make("maple syrup", C::Sweetener, {"american"}, {1, 0, 0, 0, 0, 0, 0, .3}, kVegan, 260, 15, "amber", {}, Both),
        make("palm sugar", C::Sweetener, {"thai", "indian"}, {1, 0, 0, 0, 0, 0, 0, .3}, kVegan, 375, 10, "brown", {}, Both),
        make("salted caramel", C::Sweetener, {"french", "american"}, {.9, .5, 0, .1, 0, 0, .7, .2}, kVeg, 380, 25, "amber", {}, DessertOnly),

        // ---- Dessert bases (starch slot) ------------------------------------------------
        make("almond sablé", C::Starch, {"french"}, {.6, .1, 0, 0, 0, 0, .8, .2}, Diet{false, true, false, true}, 500, 40, "golden", {}, DessertOnly),
        make("buttery shortbread crumb", C::Starch, {"american", "french"}, {.6, .2, 0, 0, 0, 0, .8, .1}, kVegGluten, 502, 40, "golden", {}, DessertOnly),
        make("meringue shards", C::Starch, {"french", "italian"}, {1, 0, 0, 0, 0, 0, 0, .1}, kVeg, 390, 15, "white", {}, DessertOnly),
        make("coconut sticky rice", C::Starch, {"thai"}, {.6, .1, 0, 0, 0, 0, .6, .3}, kVegan, 230, 80, "white", {}, DessertOnly),
    };

    classics_ = {
        {"cherry tomatoes", "basil"}, {"cherry tomatoes", "burrata"}, {"burrata", "basil"},
        {"lemon", "garlic"}, {"lemon", "thyme"}, {"salmon fillet", "white miso"},
        {"salmon fillet", "lemon"}, {"lamb rack", "rosemary"}, {"lamb rack", "mint"},
        {"pork belly", "star anise"}, {"pork belly", "soy sauce"}, {"duck breast", "blood orange"},
        {"duck breast", "star anise"}, {"scallops", "peas"}, {"scallops", "brown butter"},
        {"beef sirloin", "red wine jus"}, {"beef sirloin", "wild mushrooms"},
        {"chicken thigh", "lemon"}, {"chicken thigh", "garlic"}, {"chicken thigh", "tarragon"},
        {"paneer", "baby spinach"}, {"paneer", "garam masala"}, {"chickpeas", "cumin"},
        {"chickpeas", "tahini"}, {"king prawns", "lime"}, {"king prawns", "lemongrass"},
        {"king prawns", "garlic"}, {"firm tofu", "gochujang"}, {"firm tofu", "white miso"},
        {"butternut squash", "brown butter"}, {"butternut squash", "thyme"},
        {"asparagus", "parmesan"}, {"wild mushrooms", "thyme"}, {"shiitake mushrooms", "soy sauce"},
        {"aubergine", "white miso"}, {"aubergine", "tahini"}, {"beetroot", "feta"},
        {"heritage carrots", "honey"}, {"charred corn", "lime"}, {"charred corn", "chipotle"},
        {"mango", "lime"}, {"mango", "coconut sticky rice"}, {"mango", "chilli"},
        {"strawberries", "aged balsamic"}, {"strawberries", "double cream"},
        {"poached pear", "dark chocolate"}, {"poached pear", "cinnamon"},
        {"dark chocolate", "salted caramel"}, {"dark chocolate", "cardamom"},
        {"figs", "honey"}, {"figs", "mascarpone"}, {"blood orange", "toasted pistachios"},
        {"yuzu", "meringue shards"}, {"vanilla bean", "double cream"},
        {"ginger", "spring onions"}, {"garlic", "ginger"}, {"coriander", "lime"},
        {"fennel", "blood orange"}, {"fennel", "salmon fillet"}, {"basil", "parmesan"},
        {"pomegranate", "greek yoghurt"}, {"pomegranate", "mint"},
    };
}

const Ingredient* KnowledgeBase::find(const std::string& name) const {
    const std::string key = toLower(name);
    for (const auto& i : ingredients_)
        if (i.name == key) return &i;
    // Fall back to a substring match ("salmon" -> "salmon fillet").
    for (const auto& i : ingredients_)
        if (!key.empty() && i.name.find(key) != std::string::npos) return &i;
    return nullptr;
}

std::vector<std::string> KnowledgeBase::cuisines() const {
    std::set<std::string> all;
    for (const auto& i : ingredients_)
        all.insert(i.cuisines.begin(), i.cuisines.end());
    return {all.begin(), all.end()};
}

double KnowledgeBase::classicPairing(const std::string& a, const std::string& b) const {
    for (const auto& [x, y] : classics_) {
        const bool match = (a.find(x) != std::string::npos && b.find(y) != std::string::npos) ||
                           (a.find(y) != std::string::npos && b.find(x) != std::string::npos);
        if (match) return 1.0;
    }
    return 0.0;
}

} // namespace chef
