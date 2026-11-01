// Offline unit tests: similarity maths and chunking. No Docker/Ollama required.

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

#include "rag/chunker.hpp"
#include "rag/similarity.hpp"

static int failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::cerr << __FILE__ << ":" << __LINE__ << ": FAILED: " #cond "\n"; \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

static bool near(double a, double b) { return std::fabs(a - b) < 1e-6; }

static void test_cosine() {
    CHECK(near(rag::cosine_similarity({1, 0}, {1, 0}), 1.0));
    CHECK(near(rag::cosine_similarity({1, 0}, {0, 1}), 0.0));
    CHECK(near(rag::cosine_similarity({1, 2}, {-1, -2}), -1.0));
    CHECK(near(rag::cosine_similarity({1, 2, 3}, {2, 4, 6}), 1.0));  // magnitude ignored
    CHECK(near(rag::cosine_similarity({0, 0}, {1, 1}), 0.0));        // zero vector safe
    CHECK(near(rag::norm(rag::normalize({3, 4})), 1.0));

    bool threw = false;
    try { rag::dot({1, 2}, {1}); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
}

static void test_chunking() {
    CHECK(rag::normalize_whitespace("  a \n\n b\t c  ") == "a b c");

    const auto chunks = rag::chunk_text("w1 w2 w3 w4 w5 w6 w7", 3, 1);
    CHECK(chunks.size() == 3);
    CHECK(chunks[0].text == "w1 w2 w3");
    CHECK(chunks[1].text == "w3 w4 w5");
    CHECK(chunks[2].text == "w5 w6 w7");
    CHECK(chunks[2].index == 2);

    CHECK(rag::chunk_text("", 3, 1).empty());
    CHECK(rag::chunk_text("one two", 5, 2).size() == 1);

    bool threw = false;
    try { rag::chunk_text("x", 3, 3); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
}

static void test_ids() {
    CHECK(rag::stable_id("a.txt", 0) == rag::stable_id("a.txt", 0));
    CHECK(rag::stable_id("a.txt", 0) != rag::stable_id("a.txt", 1));
    CHECK(rag::stable_id("a.txt", 0) != rag::stable_id("b.txt", 0));
}

int main() {
    test_cosine();
    test_chunking();
    test_ids();
    if (failures) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "All tests passed\n";
    return 0;
}
