// Language-model abstraction used by every feature.
#pragma once

#include <stdexcept>
#include <string>

namespace wa {

struct CompletionRequest {
    std::string task;          // feature id, e.g. "summarize" (used by the offline client)
    std::string systemPrompt;  // instructions
    std::string userPrompt;    // the text plus task-specific framing
    std::string input;         // the raw text the user supplied
    int maxTokens = 16000;
};

struct CompletionResult {
    std::string text;
    std::string model;
    std::string stopReason;
    int inputTokens = 0;
    int outputTokens = 0;
};

// The API returned an error status.
class LLMError : public std::runtime_error {
public:
    LLMError(const std::string& msg, int status = 0) : std::runtime_error(msg), status_(status) {}
    int status() const { return status_; }

private:
    int status_;
};

// The model declined the request (stop_reason == "refusal").
class RefusalError : public LLMError {
public:
    RefusalError(const std::string& msg, std::string category)
        : LLMError(msg, 200), category_(std::move(category)) {}
    const std::string& category() const { return category_; }

private:
    std::string category_;
};

class ILLMClient {
public:
    virtual ~ILLMClient() = default;
    virtual CompletionResult complete(const CompletionRequest& request) = 0;
    virtual std::string name() const = 0;
};

}  // namespace wa
