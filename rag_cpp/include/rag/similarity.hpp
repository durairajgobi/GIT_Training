#pragma once
// Semantic search maths: an embedding is just a vector of floats, and two texts
// are "semantically close" when their vectors point in a similar direction.

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace rag {

using Embedding = std::vector<float>;

inline double dot(const Embedding& a, const Embedding& b) {
    if (a.size() != b.size())
        throw std::invalid_argument("dot: vectors have different dimensions");
    double sum = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) sum += static_cast<double>(a[i]) * b[i];
    return sum;
}

inline double norm(const Embedding& v) { return std::sqrt(dot(v, v)); }

// cos(theta) = (a . b) / (|a| * |b|)  -> range [-1, 1]; 1 means same direction.
inline double cosine_similarity(const Embedding& a, const Embedding& b) {
    const double denom = norm(a) * norm(b);
    if (denom == 0.0) return 0.0;
    return dot(a, b) / denom;
}

// Scale a vector to unit length (after this, dot product == cosine similarity).
inline Embedding normalize(Embedding v) {
    const double n = norm(v);
    if (n == 0.0) return v;
    for (auto& x : v) x = static_cast<float>(x / n);
    return v;
}

}  // namespace rag
