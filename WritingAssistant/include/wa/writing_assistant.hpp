// Application service: looks up a feature, builds its request, and runs it
// through the injected language-model client.
#pragma once

#include <memory>

#include "wa/config.hpp"
#include "wa/feature.hpp"
#include "wa/llm_client.hpp"
#include "wa/service_container.hpp"

namespace wa {

class WritingAssistant {
public:
    WritingAssistant(std::shared_ptr<ILLMClient> llm, std::shared_ptr<FeatureRegistry> features);

    CompletionResult run(const std::string& featureId, const std::string& text, const FeatureOptions& opts = {});

    const FeatureRegistry& features() const { return *features_; }
    const ILLMClient& client() const { return *llm_; }

private:
    std::shared_ptr<ILLMClient> llm_;
    std::shared_ptr<FeatureRegistry> features_;
};

// Composition root: registers every service the app needs. Anything already
// registered (e.g. a fake IHttpClient in tests) is left as is.
void registerServices(ServiceContainer& container, const AppConfig& config);

}  // namespace wa
