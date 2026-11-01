// Deterministic, rule-based ILLMClient. It lets the app, the demos and CI run
// with no API key and no network. Output is a simple heuristic, not AI.
#pragma once

#include "wa/llm_client.hpp"

namespace wa {

class OfflineClient : public ILLMClient {
public:
    CompletionResult complete(const CompletionRequest& request) override;
    std::string name() const override { return "offline"; }

    // Heuristics, exposed for tests.
    static std::string summarize(const std::string& text, size_t maxSentences);
    static std::string proofread(const std::string& text);
};

}  // namespace wa
