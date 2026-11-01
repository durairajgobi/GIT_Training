#include "wa/writing_assistant.hpp"

#include "wa/anthropic_client.hpp"
#include "wa/http_client.hpp"
#include "wa/offline_client.hpp"

namespace wa {

WritingAssistant::WritingAssistant(std::shared_ptr<ILLMClient> llm, std::shared_ptr<FeatureRegistry> features)
    : llm_(std::move(llm)), features_(std::move(features)) {
    if (!llm_ || !features_) throw std::invalid_argument("WritingAssistant needs an LLM client and features");
}

CompletionResult WritingAssistant::run(const std::string& featureId, const std::string& text,
                                       const FeatureOptions& opts) {
    auto feature = features_->find(featureId);
    if (!feature) throw ValidationError("unknown feature '" + featureId + "'");
    return llm_->complete(feature->buildRequest(text, opts));
}

void registerServices(ServiceContainer& c, const AppConfig& config) {
    c.addInstance<AppConfig>(std::make_shared<AppConfig>(config));

    if (!c.has<IHttpClient>()) {
        c.addSingleton<IHttpClient>([](ServiceContainer&) { return std::make_shared<CurlHttpClient>(); });
    }

    if (!c.has<ILLMClient>()) {
        c.addSingleton<ILLMClient>([](ServiceContainer& sc) -> std::shared_ptr<ILLMClient> {
            auto cfg = sc.resolve<AppConfig>();
            const std::string provider = cfg->resolvedProvider();
            if (provider == "offline") return std::make_shared<OfflineClient>();
            if (provider != "anthropic") throw std::invalid_argument("unknown provider '" + provider + "'");
            AnthropicOptions o;
            o.apiKey = cfg->apiKey;
            o.model = cfg->model;
            o.baseUrl = cfg->baseUrl;
            o.effort = cfg->effort;
            o.serverSideFallbacks = cfg->serverSideFallbacks;
            return std::make_shared<AnthropicClient>(sc.resolve<IHttpClient>(), o);
        });
    }

    if (!c.has<FeatureRegistry>()) {
        c.addSingleton<FeatureRegistry>([](ServiceContainer&) {
            auto r = std::make_shared<FeatureRegistry>();
            r->add(std::make_shared<SummarizeFeature>());
            r->add(std::make_shared<ProofreadFeature>());
            r->add(std::make_shared<ToneFeature>());
            r->add(std::make_shared<ExpandFeature>());
            r->add(std::make_shared<TranslateFeature>());
            return r;
        });
    }

    c.addSingleton<WritingAssistant>([](ServiceContainer& sc) {
        return std::make_shared<WritingAssistant>(sc.resolve<ILLMClient>(), sc.resolve<FeatureRegistry>());
    });
}

}  // namespace wa
