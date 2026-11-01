#pragma once

#include <array>
#include <string>
#include <vector>

namespace chef {

// Flavour dimensions used by the pairing model.
enum Flavor { Sweet, Salty, Sour, Bitter, Umami, Spicy, Rich, Aromatic, kFlavorCount };

using FlavorProfile = std::array<double, kFlavorCount>;

enum class Category {
    Protein, Vegetable, Starch, Aromatic, Herb, Spice,
    Dairy, Sauce, Fruit, Nut, Sweetener, Fat
};

enum class Course { Starter, Main, Dessert };

struct Ingredient {
    std::string name;
    Category category;
    std::vector<std::string> cuisines;   // lower-case cuisine tags; empty = universal
    FlavorProfile flavor;                // each value in [0, 1]
    bool vegan;
    bool vegetarian;
    bool glutenFree;
    bool containsNuts;
    double kcalPer100g;
    double gramsPerServing;
    std::string color;                   // used for the presentation score
    std::vector<std::string> techniques; // preferred cooking techniques (hero items)
    bool savory = true;                  // allowed in starters / mains
    bool dessert = false;                // allowed in desserts
};

struct Preferences {
    std::string cuisine;                 // empty = any
    Course course = Course::Main;
    bool vegan = false;
    bool vegetarian = false;
    bool glutenFree = false;
    bool nutFree = false;
    std::vector<std::string> mustUse;
    std::vector<std::string> avoid;
    int serves = 2;
    double creativity = 0.5;             // 0 = classic, 1 = experimental
};

std::string toString(Category c);
std::string toString(Course c);
bool parseCourse(const std::string& s, Course& out);

} // namespace chef
