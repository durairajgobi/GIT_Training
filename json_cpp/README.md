# JSON Operations in C++

A small, dependency-free JSON library for C++17, with a demo program and unit tests.

## Files

| File | Purpose |
|------|---------|
| `json.hpp` / `json.cpp` | The library (`sj::Json`) |
| `main.cpp` | Demo of every operation |
| `test_json.cpp` | Unit tests |
| `sample.json` | Sample input file |
| `Makefile`, `CMakeLists.txt` | Build scripts |

## Build and run

```bash
make            # builds json_demo and test_json
make test       # runs unit tests
make run        # runs the demo on sample.json
```

Or with CMake:

```bash
cmake -S . -B build && cmake --build build && ./build/json_demo sample.json
```

Or directly:

```bash
g++ -std=c++17 -Wall -O2 main.cpp json.cpp -o json_demo
```

## Supported operations

```cpp
#include "json.hpp"
using sj::Json;

Json doc = Json::parse(R"({"name":"Asha","tags":["a","b"]})");   // parse
Json cfg = Json::loadFile("config.json");                        // read file

std::string name = doc["name"].asString();                        // read
long long n      = doc["count"].isNull() ? 0 : doc["count"].asInt();
int port         = cfg.valueOr<int>("port", 8080);                // with default
const Json* t    = doc.pointer("/tags/1");                         // JSON Pointer

doc["age"] = 30;                                                  // add / update
doc["address"]["city"] = "Chennai";                               // nested auto-create
doc["tags"].push_back("c");                                       // append to array
doc["tags"].insert(0, "first");
doc["tags"].eraseAt(1);                                           // remove from array
doc.erase("age");                                                 // remove key

for (const auto& [key, value] : doc.asObject()) { /* ... */ }      // iterate object
for (const Json& item : doc["tags"].asArray())  { /* ... */ }      // iterate array

doc.merge(Json::parse(R"({"address":{"zip":"600001"}})"));        // deep merge
bool same = (doc == other);                                       // key order ignored

std::string compact = doc.dump();                                 // serialize
std::string pretty  = doc.dump(2);
doc.saveFile("out.json");                                         // write file
```

Errors (bad syntax, wrong type, missing key, index out of range) throw
`sj::JsonError`; parse errors include the line and column.

## Notes

- Strict RFC 8259 parsing: no trailing commas, comments, single quotes or leading zeros.
- `\uXXXX` escapes, including surrogate pairs, are decoded to UTF-8.
- Object keys keep insertion order; duplicate keys keep the last value.
- Numbers are stored as `double` (exact integers up to 2^53).
- Nesting depth is limited to 512 to avoid stack overflow on hostile input.
