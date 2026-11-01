# AI MasterChef Recipe Generator (C++)

A command-line chef that invents original MasterChef-style dishes (starter, main or dessert)
from your cuisine, dietary needs and "mystery box" ingredients, then scores them at a judges' table.

It has two engines:

| Engine | What it does | Needs |
|---|---|---|
| `local` (default) | Offline AI model: builds the dish slot by slot from a flavour knowledge base, scoring every candidate ingredient and sampling with a creativity-controlled softmax. | Nothing but a C++17 compiler |
| `claude` | The local model drafts an ingredient set, then Claude (`claude-opus-5-5`) develops it into a full restaurant-quality recipe. Falls back to `local` on any error. | `curl` and `ANTHROPIC_API_KEY` |

## Build & test

```bash
cd recipe_generator
cmake -S . -B build
cmake --build build -j
./build/chef_tests
```

## Usage

```bash
./build/masterchef --course main --cuisine japanese --use salmon
./build/masterchef --course dessert --vegan --creativity 0.9 --count 3
./build/masterchef --course starter --cuisine italian --avoid nuts,garlic --serves 4
./build/masterchef --interactive
./build/masterchef --list          # show the pantry and supported cuisines
```

With Claude:

```bash
export ANTHROPIC_API_KEY=sk-ant-...
./build/masterchef --engine claude --cuisine indian --use paneer,spinach
```

Run `./build/masterchef --help` for every option (`--gluten-free`, `--nut-free`, `--seed`, ...).

### Sample output (local engine)

```
================================================================
  Tandoor-Charred Paneer with Baby Spinach & Tamarind
================================================================
An indian-inspired main built around paneer, ...

INGREDIENTS
  - 300 g                     paneer  [hero]
  - 180 g                     garlic naan  [starch]
  ...
METHOD
  1. Take the paneer out of the fridge 20 minutes ahead, ...
JUDGES' TABLE
  Taste judge         6.7/10  "Lovely flavours, though one more hit of acidity would lift it."
```

## How the local AI chef works

1. **Knowledge base** (`src/KnowledgeBase.cpp`) – ~100 ingredients, each with an
   8-dimensional flavour profile (sweet, salty, sour, bitter, umami, heat, richness, aroma),
   cuisine tags, diet flags, calories, colour and preferred techniques, plus a list of classic pairings.
2. **Dish template** – each course is a set of slots (hero, starch, vegetable, aromatic, sauce, garnish…).
   Mystery-box ingredients are placed first.
3. **Scoring** – each candidate is scored by
   `2·cuisine fit + 1.5·mean pairing with chosen ingredients + 0.15·balance gain`, where pairing rewards
   harmony (cosine similarity), contrast (acid vs. fat, sweet vs. heat, salt vs. sweet), classic pairings,
   and penalises clashes (bitter + bitter, salt + salt).
4. **Sampling** – a softmax over scores; `--creativity` sets the temperature (classic ↔ experimental) and
   also relaxes the cuisine penalty to allow fusion.
5. **Method, plating & naming** – generated from the hero's technique and the chosen ingredients.
6. **Judges** – taste (dish balance vs. a target flavour profile + pairing quality), creativity
   (off-cuisine ingredients, fewer textbook pairings) and presentation (colour variety).

## Claude engine notes

C++ has no official Anthropic SDK, so `src/ClaudeClient.cpp` calls the Messages API over HTTPS with the
system `curl`. The API key is written to a private (0600) temp file passed as `-H @file`, so it never
appears in the process list. The request uses adaptive thinking with `effort: medium`, and opts in to
server-side refusal fallbacks (`fallbacks: "default"`). If the API returns an error or a refusal, the
program prints the reason and shows the local recipe instead. `ANTHROPIC_BASE_URL` overrides the endpoint.

## Project layout

```
recipe_generator/
├── CMakeLists.txt
├── src/
│   ├── Ingredient.hpp        data types and preferences
│   ├── KnowledgeBase.*       ingredients, flavour profiles, classic pairings
│   ├── RecipeEngine.*        scoring, sampling, method writing, judging
│   ├── ClaudeClient.*        Claude Messages API client and prompts
│   ├── Json.hpp              minimal JSON parser/serialiser
│   └── main.cpp              command-line interface
└── tests/test_main.cpp       unit tests (diet rules, mystery box, determinism, JSON, ...)
```
