// A writing feature turns user text plus options into a prompt, sends it to
// the injected ILLMClient, and returns the result.
#pragma once

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "wa/llm_client.hpp"

namespace wa {

using FeatureOptions = std::map<std::string, std::string>;

class ValidationError : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

struct OptionSpec {
    std::string name;
    std::string description;
    std::string defaultValue;
    std::vector<std::string> allowed;  // empty = free text
};

class IFeature {
public:
    virtual ~IFeature() = default;
    virtual std::string id() const = 0;
    virtual std::string description() const = 0;
    virtual std::vector<OptionSpec> options() const { return {}; }
    // Builds the prompt. Throws ValidationError for bad input/options.
    virtual CompletionRequest buildRequest(const std::string& text, const FeatureOptions& opts) const = 0;
};

// Shared validation / option handling. Features only describe their prompt.
class FeatureBase : public IFeature {
public:
    CompletionRequest buildRequest(const std::string& text, const FeatureOptions& opts) const final;

protected:
    // Called with options already validated and defaults filled in.
    virtual void fill(CompletionRequest& req, const FeatureOptions& opts) const = 0;
};

class FeatureRegistry {
public:
    void add(std::shared_ptr<IFeature> feature);
    std::shared_ptr<IFeature> find(const std::string& id) const;
    std::vector<std::shared_ptr<IFeature>> all() const;

private:
    std::vector<std::shared_ptr<IFeature>> features_;
};

// The five features.
class SummarizeFeature : public FeatureBase {
public:
    std::string id() const override { return "summarize"; }
    std::string description() const override { return "Condense text to its key points"; }
    std::vector<OptionSpec> options() const override;

protected:
    void fill(CompletionRequest& req, const FeatureOptions& opts) const override;
};

class ProofreadFeature : public FeatureBase {
public:
    std::string id() const override { return "proofread"; }
    std::string description() const override { return "Fix grammar, spelling and punctuation"; }
    std::vector<OptionSpec> options() const override;

protected:
    void fill(CompletionRequest& req, const FeatureOptions& opts) const override;
};

class ToneFeature : public FeatureBase {
public:
    std::string id() const override { return "tone"; }
    std::string description() const override { return "Rewrite text in a different tone"; }
    std::vector<OptionSpec> options() const override;

protected:
    void fill(CompletionRequest& req, const FeatureOptions& opts) const override;
};

class ExpandFeature : public FeatureBase {
public:
    std::string id() const override { return "expand"; }
    std::string description() const override { return "Develop notes or a short draft into fuller prose"; }
    std::vector<OptionSpec> options() const override;

protected:
    void fill(CompletionRequest& req, const FeatureOptions& opts) const override;
};

class TranslateFeature : public FeatureBase {
public:
    std::string id() const override { return "translate"; }
    std::string description() const override { return "Translate text into another language"; }
    std::vector<OptionSpec> options() const override;

protected:
    void fill(CompletionRequest& req, const FeatureOptions& opts) const override;
};

}  // namespace wa
