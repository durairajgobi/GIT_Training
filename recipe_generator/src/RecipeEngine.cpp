#include "RecipeEngine.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <numeric>
#include <set>
#include <sstream>

namespace chef {

namespace {

constexpr const char* kFlavorNames[kFlavorCount] = {
    "sweet", "salty", "sour", "bitter", "umami", "heat", "richness", "aroma"};

double cosine(const FlavorProfile& a, const FlavorProfile& b) {
    double dot = 0, na = 0, nb = 0;
    for (int k = 0; k < kFlavorCount; ++k) {
        dot += a[k] * b[k];
        na += a[k] * a[k];
        nb += b[k] * b[k];
    }
    if (na == 0 || nb == 0) return 0;
    return dot / (std::sqrt(na) * std::sqrt(nb));
}

FlavorProfile targetFor(Course c) {
    //                      sweet salty sour bitter umami heat rich aroma
    switch (c) {
        case Course::Starter: return {.2, .3, .45, .1, .4, .15, .35, .5};
        case Course::Main:    return {.15, .3, .25, .1, .55, .15, .45, .45};
        case Course::Dessert: return {.8, .05, .3, .1, 0, .05, .5, .4};
    }
    return {};
}

std::string titleCase(const std::string& s) {
    std::string out = s;
    bool start = true;
    for (char& ch : out) {
        if (start && std::isalpha(static_cast<unsigned char>(ch)))
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        start = (ch == ' ' || ch == '-');
    }
    return out;
}

std::string joinNames(const std::vector<std::string>& names) {
    if (names.empty()) return "";
    if (names.size() == 1) return names[0];
    std::string out;
    for (size_t i = 0; i < names.size(); ++i) {
        if (i > 0) out += (i + 1 == names.size()) ? " and " : ", ";
        out += names[i];
    }
    return out;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

} // namespace

RecipeEngine::RecipeEngine(const KnowledgeBase& kb, unsigned seed) : kb_(kb), rng_(seed) {}

// ---------------------------------------------------------------------------
// Scoring model
// ---------------------------------------------------------------------------

double RecipeEngine::pairingScore(const Ingredient& a, const Ingredient& b) const {
    const auto& x = a.flavor;
    const auto& y = b.flavor;
    // Similar flavours harmonise...
    const double harmony = cosine(x, y);
    // ...while contrasting ones lift each other (acid cuts fat, sweet tames heat, etc.).
    double contrast = x[Rich] * y[Sour] + x[Sour] * y[Rich] +
                      x[Sweet] * y[Spicy] + x[Spicy] * y[Sweet] +
                      0.5 * (x[Umami] * y[Sour] + x[Sour] * y[Umami]) +
                      0.5 * (x[Sweet] * y[Salty] + x[Salty] * y[Sweet]);
    // Too much of one harsh note clashes.
    const double clash = x[Bitter] * y[Bitter] + 0.5 * x[Salty] * y[Salty] + 0.5 * x[Spicy] * y[Spicy];
    const double classic = kb_.classicPairing(a.name, b.name);
    return 0.4 * harmony + 0.6 * contrast - 0.7 * clash + 1.0 * classic;
}

double RecipeEngine::balanceScore(const FlavorProfile& p, Course course) {
    const double sim = cosine(p, targetFor(course));
    return std::clamp(10.0 * (sim - 0.6) / 0.4, 0.0, 10.0);
}

FlavorProfile RecipeEngine::profileOf(const std::vector<RecipeLine>& lines) {
    FlavorProfile p{};
    double total = 0;
    for (const auto& l : lines) {
        const double w = (l.role == "hero") ? 1.5 : 1.0;
        for (int k = 0; k < kFlavorCount; ++k) p[k] += w * l.ingredient->flavor[k];
        total += w;
    }
    if (total > 0)
        for (double& v : p) v /= total;
    return p;
}

bool RecipeEngine::allowed(const Ingredient& i, const Preferences& prefs) const {
    if (prefs.course == Course::Dessert ? !i.dessert : !i.savory) return false;
    if (prefs.vegan && !i.vegan) return false;
    if (prefs.vegetarian && !i.vegetarian) return false;
    if (prefs.glutenFree && !i.glutenFree) return false;
    if (prefs.nutFree && i.containsNuts) return false;
    for (const auto& a : prefs.avoid) {
        const std::string key = toLower(a);
        if (key.empty()) continue;
        if (contains(i.name, key) || toLower(toString(i.category)) == key) return false;
        if ((key == "nuts" || key == "nut") && i.containsNuts) return false;
        if ((key == "dairy") && i.category == Category::Dairy) return false;
        if ((key == "meat" || key == "fish" || key == "seafood") && !i.vegetarian &&
            i.category == Category::Protein) return false;
    }
    return true;
}

double RecipeEngine::candidateScore(const Ingredient& cand, const Preferences& prefs,
                                    const std::vector<RecipeLine>& chosen) const {
    double cuisineFit = 0.5;
    if (!prefs.cuisine.empty()) {
        const std::string want = toLower(prefs.cuisine);
        if (cand.cuisines.empty())
            cuisineFit = 0.8;
        else if (std::find(cand.cuisines.begin(), cand.cuisines.end(), want) != cand.cuisines.end())
            cuisineFit = 1.5;
        else
            cuisineFit = -1.0 * (1.0 - prefs.creativity);  // fusion is fine when feeling creative
    }

    double pairing = 0;
    for (const auto& l : chosen) pairing += pairingScore(cand, *l.ingredient);
    if (!chosen.empty()) pairing /= static_cast<double>(chosen.size());

    std::vector<RecipeLine> with = chosen;
    with.push_back({&cand, 0, ""});
    const double before = chosen.empty() ? 0 : balanceScore(profileOf(chosen), prefs.course);
    const double gain = balanceScore(profileOf(with), prefs.course) - before;

    return 2.0 * cuisineFit + 1.5 * pairing + 0.15 * gain;
}

std::vector<RecipeEngine::Slot> RecipeEngine::slotsFor(Course course) const {
    using C = Category;
    switch (course) {
        case Course::Starter:
            return {{"hero", {C::Protein, C::Vegetable}, false},
                    {"side", {C::Vegetable, C::Fruit}, false},
                    {"aromatic", {C::Aromatic}, true},
                    {"acid", {C::Sauce}, false},
                    {"fat", {C::Fat, C::Dairy}, false},
                    {"garnish", {C::Herb, C::Nut}, false}};
        case Course::Dessert:
            return {{"hero", {C::Fruit}, false},
                    {"base", {C::Starch}, false},
                    {"cream", {C::Dairy}, false},
                    {"sweetener", {C::Sweetener}, false},
                    {"spice", {C::Spice, C::Herb}, true},
                    {"acid", {C::Sauce}, true},
                    {"crunch", {C::Nut}, true}};
        case Course::Main:
        default:
            return {{"hero", {C::Protein}, false},
                    {"starch", {C::Starch}, false},
                    {"vegetable", {C::Vegetable}, false},
                    {"vegetable", {C::Vegetable}, true},
                    {"aromatic", {C::Aromatic}, false},
                    {"aromatic", {C::Aromatic}, true},
                    {"spice", {C::Spice}, true},
                    {"fat", {C::Fat}, false},
                    {"sauce", {C::Sauce, C::Dairy}, false},
                    {"garnish", {C::Herb, C::Nut}, false}};
    }
}

const Ingredient* RecipeEngine::pick(const Slot& slot, const Preferences& prefs,
                                     const std::vector<RecipeLine>& chosen) {
    std::vector<const Ingredient*> cands;
    std::vector<double> scores;
    for (const auto& ing : kb_.ingredients()) {
        if (std::find(slot.categories.begin(), slot.categories.end(), ing.category) == slot.categories.end())
            continue;
        if (!allowed(ing, prefs)) continue;
        const bool used = std::any_of(chosen.begin(), chosen.end(),
                                      [&](const RecipeLine& l) { return l.ingredient == &ing; });
        if (used) continue;
        cands.push_back(&ing);
        scores.push_back(candidateScore(ing, prefs, chosen));
    }
    if (cands.empty()) return nullptr;

    // Softmax sampling: low creativity -> greedy/classic, high -> adventurous.
    const double temperature = 0.2 + 1.0 * std::clamp(prefs.creativity, 0.0, 1.0);
    const double best = *std::max_element(scores.begin(), scores.end());
    std::vector<double> weights;
    weights.reserve(scores.size());
    for (double s : scores) weights.push_back(std::exp((s - best) / temperature));
    std::discrete_distribution<size_t> dist(weights.begin(), weights.end());
    return cands[dist(rng_)];
}

// ---------------------------------------------------------------------------
// Generation
// ---------------------------------------------------------------------------

Recipe RecipeEngine::generate(const Preferences& prefs) {
    if (prefs.serves < 1 || prefs.serves > 50) throw GenerationError("serves must be between 1 and 50");

    Recipe r;
    r.course = prefs.course;
    r.serves = prefs.serves;

    std::vector<Slot> slots = slotsFor(prefs.course);
    std::vector<bool> filled(slots.size(), false);
    std::vector<RecipeLine> chosen;

    // 1. Mystery-box ingredients the contestant must use.
    for (const auto& name : prefs.mustUse) {
        const Ingredient* ing = kb_.find(name);
        if (!ing) throw GenerationError("Unknown ingredient '" + name + "'. Run with --list to see the pantry.");
        if (!allowed(*ing, prefs))
            throw GenerationError("'" + ing->name + "' conflicts with the chosen course, diet or avoid list.");
        if (std::any_of(chosen.begin(), chosen.end(), [&](const RecipeLine& l) { return l.ingredient == ing; }))
            continue;
        std::string role = "extra";
        for (size_t s = 0; s < slots.size(); ++s) {
            const auto& cats = slots[s].categories;
            if (!filled[s] && std::find(cats.begin(), cats.end(), ing->category) != cats.end()) {
                filled[s] = true;
                role = slots[s].role;
                break;
            }
        }
        chosen.push_back({ing, ing->gramsPerServing * prefs.serves, role});
    }

    // 2. Fill the remaining slots, hero first so everything pairs around it.
    std::uniform_real_distribution<double> coin(0.0, 1.0);
    for (size_t s = 0; s < slots.size(); ++s) {
        if (filled[s]) continue;
        if (slots[s].optional && coin(rng_) > 0.45 + 0.4 * prefs.creativity) continue;
        if (const Ingredient* ing = pick(slots[s], prefs, chosen)) {
            chosen.push_back({ing, ing->gramsPerServing * prefs.serves, slots[s].role});
            filled[s] = true;
        } else if (!slots[s].optional) {
            throw GenerationError("Not enough ingredients for the '" + slots[s].role +
                                  "' slot with these restrictions. Try relaxing --avoid or the diet.");
        }
    }

    // Keep the hero at the top of the list.
    std::stable_sort(chosen.begin(), chosen.end(), [](const RecipeLine& a, const RecipeLine& b) {
        return a.role == "hero" && b.role != "hero";
    });
    r.lines = chosen;
    r.profile = profileOf(chosen);
    r.balanceScore = balanceScore(r.profile, prefs.course);

    double kcal = 0;
    for (const auto& l : chosen) kcal += l.ingredient->kcalPer100g * l.grams / 100.0;
    r.kcalPerServing = kcal / prefs.serves;

    // Dominant cuisine.
    if (!prefs.cuisine.empty()) {
        r.cuisine = toLower(prefs.cuisine);
    } else {
        std::map<std::string, int> votes;
        for (const auto& l : chosen)
            for (const auto& c : l.ingredient->cuisines) ++votes[c];
        int bestVotes = 0;
        for (const auto& [c, v] : votes)
            if (v > bestVotes) { bestVotes = v; r.cuisine = c; }
        if (bestVotes * 2 < static_cast<int>(chosen.size())) r.cuisine = "modern fusion";
    }

    // Technique comes from the hero ingredient.
    const Ingredient* hero = chosen.front().ingredient;
    if (!hero->techniques.empty()) {
        std::uniform_int_distribution<size_t> t(0, hero->techniques.size() - 1);
        r.technique = hero->techniques[t(rng_)];
    } else {
        r.technique = prefs.course == Course::Dessert ? "assembled" : "roasted";
    }

    writeMethod(r);
    nameDish(r);
    judge(r);
    return r;
}

// ---------------------------------------------------------------------------
// Method writing
// ---------------------------------------------------------------------------

namespace {

std::vector<std::string> namesFor(const Recipe& r, std::initializer_list<const char*> roles) {
    std::vector<std::string> out;
    for (const auto& l : r.lines)
        for (const char* role : roles)
            if (l.role == role) { out.push_back(l.ingredient->name); break; }
    return out;
}

std::string first(const std::vector<std::string>& v, const std::string& fallback) {
    return v.empty() ? fallback : v.front();
}

int cookTimeFor(const std::string& technique) {
    static const std::map<std::string, int> times = {
        {"braised", 150}, {"roasted", 35}, {"pan-seared", 15}, {"seared", 6}, {"grilled", 15},
        {"poached", 10}, {"stir-fried", 10}, {"cured", 0}, {"crispy-fried", 12}, {"glazed", 15},
        {"tandoor-charred", 15}, {"charred", 25}, {"torn", 0}, {"fresh", 0}, {"macerated", 0},
        {"tempered", 15}, {"curd", 15}, {"segmented", 0}, {"assembled", 10}};
    auto it = times.find(technique);
    return it == times.end() ? 20 : it->second;
}

std::string heroMethod(const std::string& technique, const std::string& hero, const std::string& fat) {
    if (technique == "pan-seared" || technique == "seared")
        return "Heat a heavy pan until smoking, add a little " + fat + " and lay the " + hero +
               " in, presentation side down. Sear without moving until deeply golden, flip, baste with the "
               "pan juices and cook to just done. Rest for 5 minutes.";
    if (technique == "roasted")
        return "Roast the " + hero + " at 200°C / 400°F, brushed with " + fat +
               ", until caramelised at the edges and cooked through. Rest before carving.";
    if (technique == "grilled")
        return "Get a griddle pan screaming hot. Oil the " + hero +
               " (not the pan) and grill for good char lines on each side. Rest briefly.";
    if (technique == "braised")
        return "Brown the " + hero + " all over, then cover with stock and the aromatics, put a lid on and "
               "braise low and slow at 160°C / 320°F for about 2½ hours until meltingly tender. "
               "Reduce the braising liquor to a glossy glaze.";
    if (technique == "stir-fried")
        return "In a smoking wok, stir-fry the " + hero + " in " + fat +
               " for 2–3 minutes until just cooked; remove and keep warm.";
    if (technique == "poached")
        return "Poach the " + hero + " in barely simmering, well-seasoned water for a few minutes until just opaque.";
    if (technique == "cured")
        return "Slice the " + hero + " thinly, sprinkle with a 50/50 salt-sugar cure for 10 minutes, rinse, pat dry and chill.";
    if (technique == "crispy-fried")
        return "Press the " + hero + " for 15 minutes, cut into cubes, toss in cornflour and shallow-fry in " +
               fat + " until crisp on all sides. Drain on paper.";
    if (technique == "glazed")
        return "Pan-fry the " + hero + " until golden, then add the sauce and toss until lacquered and sticky.";
    if (technique == "tandoor-charred")
        return "Marinate the " + hero + " in the spices and a little yoghurt for 20 minutes, then char under a "
               "fierce grill until blistered at the edges.";
    if (technique == "charred")
        return "Brush the " + hero + " with " + fat + " and roast at 220°C / 425°F, then finish in a hot pan "
               "for deep charring.";
    if (technique == "torn")
        return "Bring the " + hero + " to room temperature and tear it open just before plating.";
    return "Cook the " + hero + " with care until perfectly done.";
}

std::string starchMethod(const std::string& starch) {
    if (contains(starch, "arborio"))
        return "Make a risotto: toast the arborio rice in a little fat, then add hot stock a ladle at a time, "
               "stirring, for about 18 minutes until creamy but with bite.";
    if (contains(starch, "rice"))
        return "Rinse the " + starch + " until the water runs clear, then steam with a 1:1.2 ratio of water "
               "for 12 minutes and rest, covered, for 10.";
    if (contains(starch, "pappardelle") || contains(starch, "noodles"))
        return "Cook the " + starch + " in plenty of well-salted boiling water until al dente; "
               "reserve a cup of the starchy water.";
    if (contains(starch, "potatoes"))
        return "Boil the potatoes until tender, drain, then crush lightly with a fork and season well.";
    if (contains(starch, "purée"))
        return "Boil peeled floury potatoes until very soft, pass through a ricer and beat in cold butter "
               "and warm milk until silky.";
    if (contains(starch, "couscous"))
        return "Toast the pearl couscous in a dry pan, then simmer in stock for 8 minutes until tender.";
    if (contains(starch, "tortillas") || contains(starch, "naan"))
        return "Warm the " + starch + " in a hot, dry pan just before serving and keep wrapped in a cloth.";
    return "Prepare the " + starch + ".";
}

} // namespace

void RecipeEngine::writeMethod(Recipe& r) const {
    auto& s = r.steps;
    const std::string hero = r.lines.front().ingredient->name;
    const auto aromatics = namesFor(r, {"aromatic"});
    const auto spices = namesFor(r, {"spice"});
    const auto vegetables = namesFor(r, {"vegetable", "side"});
    const auto sauces = namesFor(r, {"sauce", "acid"});
    const auto garnish = namesFor(r, {"garnish", "crunch"});
    const auto extras = namesFor(r, {"extra"});
    std::string fat = "olive oil";
    std::vector<std::string> finishingDairy;
    for (const auto& l : r.lines) {
        if (l.role != "fat") continue;
        if (l.ingredient->category == Category::Fat) fat = l.ingredient->name;
        else finishingDairy.push_back(l.ingredient->name);
    }

    if (r.course == Course::Dessert) {
        const std::string base = first(namesFor(r, {"base"}), "biscuit");
        const std::string cream = first(namesFor(r, {"cream"}), "cream");
        const std::string sweet = first(namesFor(r, {"sweetener"}), "sugar");
        const bool crisp = contains(base, "sablé") || contains(base, "crumb") || contains(base, "meringue");
        s.push_back(crisp ? "Bake the " + base + " ahead and let it cool completely so it stays crisp."
                          : "Prepare the " + base + " and keep it warm, covered, until plating.");
        std::string heroStep = "Prepare the " + hero + " (" + r.technique + ") with a spoonful of " + sweet;
        if (!spices.empty()) heroStep += " and a hint of " + joinNames(spices);
        s.push_back(heroStep + ". Let the flavours mingle for 10 minutes.");
        std::string creamStep = "Whip the " + cream + " to soft peaks";
        if (!sauces.empty()) creamStep += ", sharpened with a little " + joinNames(sauces);
        s.push_back(creamStep + ". Chill until needed.");
        if (!garnish.empty())
            s.push_back("Roughly chop the " + joinNames(garnish) + " for texture.");
        if (!extras.empty()) s.push_back("Incorporate the " + joinNames(extras) + " where it best complements the dish.");
        s.push_back("Taste every component: it should be sweet but bright — adjust with acid or a pinch of salt.");
        r.plating = "Quenelle the " + cream + " onto a chilled plate, arrange the " + hero +
                    " around it, " + (crisp ? "scatter shards of " : "add a neat mound of ") + base +
                    (garnish.empty() ? std::string() : " and " + joinNames(garnish)) +
                    ", and finish with a final drizzle of " + sweet + ".";
    } else {
        if (r.lines.front().ingredient->category == Category::Protein && r.technique != "torn")
            s.push_back("Take the " + hero + " out of the fridge 20 minutes ahead, pat dry and season well with flaky salt.");
        if (r.course == Course::Main) {
            const std::string starch = first(namesFor(r, {"starch"}), "");
            if (!starch.empty()) s.push_back(starchMethod(starch));
        }
        if (!aromatics.empty()) {
            std::string step = "Gently sweat the " + joinNames(aromatics) + " in " + fat + " until soft and fragrant";
            if (!spices.empty()) step += ", then bloom the " + joinNames(spices) + " for 30 seconds";
            s.push_back(step + ".");
        } else if (!spices.empty()) {
            s.push_back("Toast the " + joinNames(spices) + " in a dry pan until fragrant.");
        }
        if (!vegetables.empty())
            s.push_back("Cook the " + joinNames(vegetables) + " until tender with a little colour; season and keep warm.");
        s.push_back(heroMethod(r.technique, hero, fat));
        if (!extras.empty()) s.push_back("Fold the " + joinNames(extras) + " through where it best complements the dish.");
        const bool rawHero = r.technique == "cured" || r.technique == "torn";
        if (!sauces.empty() && rawHero)
            s.push_back("Whisk the " + joinNames(sauces) + " with a splash of " + fat +
                        " into a sharp dressing. Taste for salt, acid and heat.");
        else if (!sauces.empty())
            s.push_back("Build the sauce: deglaze the pan, add the " + joinNames(sauces) +
                        ", and reduce until it coats the back of a spoon. Taste for salt, acid and heat.");
        if (!finishingDairy.empty())
            s.push_back("Finish with the " + joinNames(finishingDairy) + " just before serving for richness.");
        s.push_back("Taste everything once more — a MasterChef dish lives or dies by its seasoning.");
        r.plating = "Place the " + hero + " slightly off-centre on a warm plate" +
                    (vegetables.empty() ? std::string() : ", lean the " + joinNames(vegetables) + " against it") +
                    (sauces.empty() ? std::string() : ", spoon the sauce around rather than over") +
                    (garnish.empty() ? std::string(".") : ", and finish with " + joinNames(garnish) + ".");
    }

    r.cookMinutes = cookTimeFor(r.technique) + (r.course == Course::Main ? 15 : 5);
    r.prepMinutes = 10 + 3 * static_cast<int>(r.lines.size());
}

void RecipeEngine::nameDish(Recipe& r) {
    const std::string hero = titleCase(r.lines.front().ingredient->name);
    const std::string tech = titleCase(r.technique);
    auto firstOf = [&](std::initializer_list<const char*> roles) {
        return titleCase(first(namesFor(r, roles), ""));
    };

    if (r.course == Course::Dessert) {
        const std::string cream = firstOf({"cream"});
        const std::string base = firstOf({"base"});
        r.name = tech + " " + hero + ", " + cream + " & " + base;
    } else {
        const std::string veg = firstOf({"vegetable", "side"});
        const std::string sauce = firstOf({"sauce", "acid"});
        r.name = (tech == "Torn" || tech == "Fresh" ? hero : tech + " " + hero);
        if (!veg.empty()) r.name += " with " + veg;
        if (!sauce.empty()) r.name += (veg.empty() ? " with " : " & ") + sauce;
    }

    // Describe the two dominant flavours.
    std::vector<int> idx(kFlavorCount);
    std::iota(idx.begin(), idx.end(), 0);
    std::sort(idx.begin(), idx.end(), [&](int a, int b) { return r.profile[a] > r.profile[b]; });
    std::ostringstream d;
    const bool vowel = !r.cuisine.empty() && std::string("aeiou").find(r.cuisine[0]) != std::string::npos;
    d << (vowel ? "An " : "A ") << r.cuisine << "-inspired " << toLower(toString(r.course)) << " built around "
      << r.lines.front().ingredient->name << ", led by " << kFlavorNames[idx[0]] << " and "
      << kFlavorNames[idx[1]] << " with a counterpoint of " << kFlavorNames[idx[2]] << ".";
    r.description = d.str();
}

void RecipeEngine::judge(Recipe& r) {
    // Taste: dish balance plus how well the components pair with each other.
    double pairSum = 0;
    int pairs = 0, classics = 0;
    for (size_t i = 0; i < r.lines.size(); ++i)
        for (size_t j = i + 1; j < r.lines.size(); ++j) {
            pairSum += pairingScore(*r.lines[i].ingredient, *r.lines[j].ingredient);
            classics += kb_.classicPairing(r.lines[i].ingredient->name, r.lines[j].ingredient->name) > 0;
            ++pairs;
        }
    const double pairing = std::clamp(pairs ? 4.0 + 8.0 * pairSum / pairs : 5.0, 0.0, 10.0);
    const double taste = 0.6 * r.balanceScore + 0.4 * pairing;

    // Creativity: crossing cuisines and unusual combinations.
    int offCuisine = 0;
    for (const auto& l : r.lines) {
        const auto& c = l.ingredient->cuisines;
        if (!c.empty() && std::find(c.begin(), c.end(), r.cuisine) == c.end()) ++offCuisine;
    }
    const double offRatio = static_cast<double>(offCuisine) / static_cast<double>(r.lines.size());
    r.creativityScore = std::clamp(4.5 + 6.0 * offRatio + 0.25 * static_cast<double>(r.lines.size()) -
                                       0.5 * classics, 2.0, 10.0);

    // Presentation: colour variety on the plate.
    std::set<std::string> colors;
    for (const auto& l : r.lines) colors.insert(l.ingredient->color);
    r.presentationScore = std::clamp(1.5 + 1.2 * static_cast<double>(colors.size()), 0.0, 10.0);

    auto verdict = [](double score, const char* great, const char* good, const char* poor) {
        return std::string(score >= 8 ? great : score >= 6 ? good : poor);
    };
    r.judges = {
        {"Taste judge", taste,
         verdict(taste, "Every mouthful is balanced — salt, acid and richness all sing together.",
                 "Lovely flavours, though one more hit of acidity would lift it.",
                 "The flavours are fighting each other; rethink the balance.")},
        {"Creativity judge", r.creativityScore,
         verdict(r.creativityScore, "Bold, original thinking — this is a dish I haven't seen before.",
                 "A confident take on familiar flavours.",
                 "Safe. I want to see more of you on the plate.")},
        {"Presentation judge", r.presentationScore,
         verdict(r.presentationScore, "Stunning plate — vibrant colour and real restaurant finesse.",
                 "Neat plating, but it could use a pop of colour.",
                 "Looks a bit monochrome; think about height and colour.")},
    };
}

} // namespace chef
