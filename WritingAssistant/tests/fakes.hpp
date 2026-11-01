// Test doubles injected through the interfaces.
#pragma once

#include <deque>
#include <vector>

#include "wa/http_client.hpp"
#include "wa/llm_client.hpp"

namespace wt {

class FakeHttpClient : public wa::IHttpClient {
public:
    std::vector<wa::HttpRequest> requests;
    std::deque<wa::HttpResponse> responses;
    int transportFailures = 0;  // throw TransportError this many times first

    wa::HttpResponse send(const wa::HttpRequest& r) override {
        requests.push_back(r);
        if (transportFailures > 0) {
            --transportFailures;
            throw wa::TransportError("simulated network failure");
        }
        if (responses.empty()) return {500, "{}"};
        auto resp = responses.front();
        responses.pop_front();
        return resp;
    }
};

class RecordingLLMClient : public wa::ILLMClient {
public:
    std::vector<wa::CompletionRequest> requests;
    std::string reply = "fake reply";

    wa::CompletionResult complete(const wa::CompletionRequest& r) override {
        requests.push_back(r);
        wa::CompletionResult res;
        res.text = reply;
        res.model = "fake";
        res.stopReason = "end_turn";
        return res;
    }
    std::string name() const override { return "fake"; }
};

}  // namespace wt
