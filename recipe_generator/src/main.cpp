// AI-based MasterChef Recipe Generator
//
// Two engines:
//   local  - offline flavour-pairing model (default, no network needed)
//   claude - the local model drafts an ingredient set, then Claude writes the full recipe

#include "ClaudeClient.hpp"
#include "KnowledgeBase.hpp"
#include "RecipeEngine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace chef;

namespace {

void printUsage() {
    std::cout <<
R"(AI MasterChef Recipe Generator

Usage: masterchef [options]

  -c, --course <starter|main|dessert>  Course to cook (default: main)
  -k, --cuisine <name>                 Cuisine style, e.g. italian, indian, japanese
  -u, --use <a,b,...>                  Mystery-box ingredients that must be used
  -a, --avoid <a,b,...>                Ingredients or groups to avoid (e.g. nuts, dairy, pork)
      --vegan | --vegetarian           Dietary filters
      --gluten-free | --nut-free
  -s, --serves <n>                     Number of servings (default: 2)
  -x, --creativity <0..1>              0 = classic pairings, 1 = experimental (default: 0.5)
  -n, --count <n>                      Number of recipes to generate (default: 1)
      --seed <n>                       Random seed for reproducible results
  -e, --engine <local|claude>          Recipe engine (default: local; claude needs ANTHROPIC_API_KEY)
  -i, --interactive                    Answer questions instead of using flags
  -l, --list                           Show the pantry (all known ingredients) and cuisines
  -h, --help                           Show this help

Examples:
  masterchef --course main --cuisine japanese --use salmon
  masterchef --course dessert --vegan --creativity 0.9 --count 3
  masterchef --engine claude --cuisine indian --use paneer,spinach
)";
}

std::vector<std::string> splitList(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        const auto b = item.find_first_not_of(' ');
        const auto e = item.find_last_not_of(' ');
        if (b != std::string::npos) out.push_back(item.substr(b, e - b + 1));
    }
    return out;
}

std::string formatQuantity(const RecipeLine& l) {
    const double g = l.grams;
    const Category c = l.ingredient->category;
    std::ostringstream q;
    if (l.ingredient->name == "garlic") {
        q << std::max(1, static_cast<int>(std::lround(g / 5.0))) << " clove(s)";
    } else if (c == Category::Spice) {
        q << std::setprecision(2) << std::max(0.25, std::round(g / 2.5 * 4) / 4) << " tsp";
    } else if (c == Category::Herb) {
        q << "a small bunch (" << std::lround(g) << " g)";
    } else if ((c == Category::Sauce || c == Category::Fat || c == Category::Sweetener) && g < 75) {
        q << std::setprecision(2) << std::max(0.5, std::round(g / 15.0 * 2) / 2) << " tbsp";
    } else {
        q << std::lround(g / 5.0) * 5 << " g";
    }
    return q.str();
}

void printRecipe(const Recipe& r, int number) {
    const std::string bar(64, '=');
    std::cout << "\n" << bar << "\n";
    if (number > 0) std::cout << "  Recipe #" << number << "\n";
    std::cout << "  " << r.name << "\n" << bar << "\n";
    std::cout << r.description << "\n\n";
    std::cout << "Course: " << toString(r.course) << "   Serves: " << r.serves
              << "   Prep: " << r.prepMinutes << " min   Cook: " << r.cookMinutes << " min"
              << "   ~" << std::lround(r.kcalPerServing) << " kcal/serving\n\n";

    std::cout << "INGREDIENTS\n";
    for (const auto& l : r.lines)
        std::cout << "  - " << std::left << std::setw(26) << formatQuantity(l) << l.ingredient->name
                  << "  [" << l.role << "]\n";

    std::cout << "\nMETHOD\n";
    for (size_t i = 0; i < r.steps.size(); ++i) std::cout << "  " << i + 1 << ". " << r.steps[i] << "\n";

    std::cout << "\nPLATING\n  " << r.plating << "\n";

    std::cout << "\nJUDGES' TABLE\n";
    double total = 0;
    for (const auto& j : r.judges) {
        std::cout << "  " << std::left << std::setw(20) << j.judge << std::fixed << std::setprecision(1)
                  << j.score << "/10  \"" << j.comment << "\"\n";
        total += j.score;
    }
    std::cout << "  Overall: " << std::fixed << std::setprecision(1) << total / r.judges.size() << "/10\n";
    std::cout.unsetf(std::ios::fixed);
    std::cout << std::setprecision(6);
}

void listPantry(const KnowledgeBase& kb) {
    std::map<std::string, std::vector<std::string>> byCat;
    for (const auto& i : kb.ingredients()) byCat[toString(i.category)].push_back(i.name);
    std::cout << "PANTRY\n";
    for (const auto& [cat, names] : byCat) {
        std::cout << "  " << cat << ": ";
        for (size_t i = 0; i < names.size(); ++i) std::cout << (i ? ", " : "") << names[i];
        std::cout << "\n";
    }
    std::cout << "\nCUISINES: ";
    const auto cuisines = kb.cuisines();
    for (size_t i = 0; i < cuisines.size(); ++i) std::cout << (i ? ", " : "") << cuisines[i];
    std::cout << "\n";
}

std::string ask(const std::string& question, const std::string& def = "") {
    std::cout << question << (def.empty() ? "" : " [" + def + "]") << ": " << std::flush;
    std::string line;
    if (!std::getline(std::cin, line)) return def;
    return line.empty() ? def : line;
}

bool yes(const std::string& s) { return !s.empty() && (s[0] == 'y' || s[0] == 'Y'); }

void interactive(Preferences& p, std::string& engine) {
    std::cout << "Welcome to the MasterChef kitchen! Let's design your dish.\n";
    Course c;
    if (parseCourse(ask("Course (starter/main/dessert)", "main"), c)) p.course = c;
    p.cuisine = ask("Cuisine (blank for any)");
    p.mustUse = splitList(ask("Mystery-box ingredients, comma separated (blank for none)"));
    p.avoid = splitList(ask("Anything to avoid? (blank for none)"));
    p.vegan = yes(ask("Vegan? (y/n)", "n"));
    if (!p.vegan) p.vegetarian = yes(ask("Vegetarian? (y/n)", "n"));
    p.glutenFree = yes(ask("Gluten-free? (y/n)", "n"));
    p.nutFree = yes(ask("Nut-free? (y/n)", "n"));
    try { p.serves = std::stoi(ask("Serves", "2")); } catch (...) {}
    try { p.creativity = std::stod(ask("Creativity 0-1", "0.5")); } catch (...) {}
    if (ClaudeClient::available()) engine = yes(ask("Let Claude write the final recipe? (y/n)", "y")) ? "claude" : "local";
}

} // namespace

int main(int argc, char** argv) {
    Preferences prefs;
    std::string engine = "local";
    int count = 1;
    unsigned seed = static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count());
    bool runInteractive = false;
    KnowledgeBase kb;

    try {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            auto next = [&]() -> std::string {
                if (i + 1 >= argc) throw std::invalid_argument("missing value for " + arg);
                return argv[++i];
            };
            if (arg == "-h" || arg == "--help") { printUsage(); return 0; }
            else if (arg == "-l" || arg == "--list") { listPantry(kb); return 0; }
            else if (arg == "-c" || arg == "--course") {
                if (!parseCourse(next(), prefs.course)) throw std::invalid_argument("course must be starter, main or dessert");
            }
            else if (arg == "-k" || arg == "--cuisine") prefs.cuisine = toLower(next());
            else if (arg == "-u" || arg == "--use") prefs.mustUse = splitList(next());
            else if (arg == "-a" || arg == "--avoid") prefs.avoid = splitList(next());
            else if (arg == "--vegan") prefs.vegan = true;
            else if (arg == "--vegetarian") prefs.vegetarian = true;
            else if (arg == "--gluten-free") prefs.glutenFree = true;
            else if (arg == "--nut-free") prefs.nutFree = true;
            else if (arg == "-s" || arg == "--serves") prefs.serves = std::stoi(next());
            else if (arg == "-x" || arg == "--creativity") prefs.creativity = std::stod(next());
            else if (arg == "-n" || arg == "--count") count = std::stoi(next());
            else if (arg == "--seed") seed = static_cast<unsigned>(std::stoul(next()));
            else if (arg == "-e" || arg == "--engine") engine = toLower(next());
            else if (arg == "-i" || arg == "--interactive") runInteractive = true;
            else throw std::invalid_argument("unknown option " + arg);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\nRun with --help for usage.\n";
        return 2;
    }

    if (runInteractive) interactive(prefs, engine);
    if (prefs.creativity < 0 || prefs.creativity > 1) { std::cerr << "Error: creativity must be 0..1\n"; return 2; }
    if (count < 1 || count > 20) { std::cerr << "Error: count must be 1..20\n"; return 2; }
    if (engine != "local" && engine != "claude") { std::cerr << "Error: engine must be local or claude\n"; return 2; }

    if (!prefs.cuisine.empty()) {
        const auto known = kb.cuisines();
        if (std::find(known.begin(), known.end(), prefs.cuisine) == known.end())
            std::cerr << "Note: '" << prefs.cuisine << "' isn't in the local pantry's cuisines; "
                      << "the local model will improvise fusion.\n";
    }

    RecipeEngine local(kb, seed);
    for (int n = 1; n <= count; ++n) {
        Recipe draft;
        try {
            draft = local.generate(prefs);
        } catch (const GenerationError& e) {
            std::cerr << "Error: " << e.what() << "\n";
            return 1;
        }

        if (engine == "claude") {
            try {
                ClaudeClient claude;
                std::cout << "\nAsking Claude to develop the recipe";
                if (count > 1) std::cout << " (" << n << "/" << count << ")";
                std::cout << "...\n\n";
                std::cout << claude.complete(chefSystemPrompt(), chefUserPrompt(prefs, draft)) << "\n";
                continue;
            } catch (const ClaudeError& e) {
                std::cerr << "Claude engine unavailable (" << e.what() << "); using the local model instead.\n";
            }
        }
        printRecipe(draft, count > 1 ? n : 0);
    }
    if (count == 1 && engine == "local") std::cout << "\n(seed " << seed << " — pass --seed to reproduce)\n";
    return 0;
}
