// main.cpp - Demonstrates common JSON operations with the sj::Json library.
//
// Build:  make            (or: g++ -std=c++17 -Wall -O2 main.cpp json.cpp -o json_demo)
// Run:    ./json_demo [path/to/file.json]

#include <iostream>

#include "json.hpp"

using sj::Json;
using sj::JsonError;

static void section(const std::string& title) {
    std::cout << "\n==== " << title << " ====\n";
}

int main(int argc, char* argv[]) {
    try {
        // ------------------------------------------------------------------
        section("1. Parse JSON from a string");
        const std::string text = R"({
            "company": "Acme Corp",
            "founded": 1999,
            "public": false,
            "rating": 4.5,
            "address": { "city": "Chennai", "zip": "600001" },
            "employees": [
                { "id": 1, "name": "Asha",  "skills": ["C++", "Python"] },
                { "id": 2, "name": "Ravi",  "skills": ["Java"] }
            ],
            "ceo": null
        })";
        Json doc = Json::parse(text);
        std::cout << "Parsed OK. Top-level keys:";
        for (const auto& k : doc.keys()) std::cout << ' ' << k;
        std::cout << '\n';

        // ------------------------------------------------------------------
        section("2. Read values");
        std::cout << "company   : " << doc["company"].asString() << '\n';
        std::cout << "founded   : " << doc["founded"].asInt() << '\n';
        std::cout << "public    : " << std::boolalpha << doc["public"].asBool() << '\n';
        std::cout << "rating    : " << doc["rating"].asNumber() << '\n';
        std::cout << "city      : " << doc["address"]["city"].asString() << '\n';
        std::cout << "ceo null? : " << doc["ceo"].isNull() << '\n';
        std::cout << "website   : " << doc.valueOr<std::string>("website", "<none>") << '\n';

        // ------------------------------------------------------------------
        section("3. Iterate over an array of objects");
        for (const Json& emp : doc["employees"].asArray()) {
            std::cout << "#" << emp.at("id").asInt() << " " << emp.at("name").asString()
                      << " skills:";
            for (const Json& s : emp.at("skills").asArray()) std::cout << ' ' << s.asString();
            std::cout << '\n';
        }

        // ------------------------------------------------------------------
        section("4. JSON Pointer lookup");
        if (const Json* p = doc.pointer("/employees/0/skills/1"))
            std::cout << "/employees/0/skills/1 = " << p->dump() << '\n';
        std::cout << "/employees/9 exists? " << (doc.pointer("/employees/9") != nullptr) << '\n';

        // ------------------------------------------------------------------
        section("5. Modify: update, add, delete");
        doc["rating"] = 4.8;                                   // update
        doc["public"] = true;
        doc["ceo"]    = "Meena";                               // null -> string
        doc["address"]["country"] = "India";                   // add nested key
        doc["tags"].push_back("software");                     // creates array
        doc["tags"].push_back("consulting");
        doc["employees"].push_back(Json::object({
            {"id", 3},
            {"name", "Karthik"},
            {"skills", Json::array({"Go", "Rust"})},
        }));
        doc["employees"][1]["skills"].push_back("Kotlin");
        doc["employees"].eraseAt(0);                           // delete array item
        doc.erase("founded");                                  // delete key
        std::cout << doc.dump(2) << '\n';

        // ------------------------------------------------------------------
        section("6. Build JSON from scratch");
        Json config;
        config["version"]           = "1.2.0";
        config["debug"]             = false;
        config["server"]["host"]    = "localhost";
        config["server"]["port"]    = 8080;
        config["server"]["origins"] = Json::array({"http://a.com", "http://b.com"});
        std::cout << "compact: " << config.dump() << '\n';

        // ------------------------------------------------------------------
        section("7. Deep merge");
        Json overrides = Json::parse(R"({"debug": true, "server": {"port": 9090}})");
        config.merge(overrides);
        std::cout << config.dump(4) << '\n';

        // ------------------------------------------------------------------
        section("8. Escapes and Unicode");
        Json u = Json::parse(R"({"msg": "Tab\there \"quoted\" ₹ 😀"})");
        std::cout << "decoded : " << u["msg"].asString() << '\n';
        std::cout << "encoded : " << u.dump() << '\n';

        // ------------------------------------------------------------------
        section("9. Compare");
        Json a = Json::parse(R"({"x":1,"y":[1,2]})");
        Json b = Json::parse(R"({"y":[1,2],"x":1})");
        std::cout << "a == b (key order ignored): " << (a == b) << '\n';

        // ------------------------------------------------------------------
        section("10. Save to and load from a file");
        const std::string outPath = "output.json";
        doc.saveFile(outPath);
        Json reloaded = Json::loadFile(outPath);
        std::cout << "Saved to " << outPath << ", reloaded equal? " << (reloaded == doc) << '\n';

        if (argc > 1) {
            Json fromArg = Json::loadFile(argv[1]);
            std::cout << "\nContents of " << argv[1] << ":\n" << fromArg.dump(2) << '\n';
        }

        // ------------------------------------------------------------------
        section("11. Error handling");
        const char* bad[] = {R"({"a": 1,})", R"([1, 2)", R"({"a": tru})", "01"};
        for (const char* s : bad) {
            try {
                Json::parse(s);
            } catch (const JsonError& e) {
                std::cout << s << "  ->  " << e.what() << '\n';
            }
        }
        try {
            doc["company"].asInt();
        } catch (const JsonError& e) {
            std::cout << "doc[\"company\"].asInt()  ->  " << e.what() << '\n';
        }
        try {
            doc.at("missing");
        } catch (const JsonError& e) {
            std::cout << "doc.at(\"missing\")  ->  " << e.what() << '\n';
        }
    } catch (const JsonError& e) {
        std::cerr << "JSON error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
