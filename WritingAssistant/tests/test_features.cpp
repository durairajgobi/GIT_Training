// Phase 2: the five features, the offline client and the assistant facade.
#include "fakes.hpp"
#include "test_framework.hpp"
#include "wa/offline_client.hpp"
#include "wa/writing_assistant.hpp"

using namespace wa;

namespace {
std::shared_ptr<FeatureRegistry> allFeatures() {
    ServiceContainer c;
    registerServices(c, AppConfig{});
    return c.resolve<FeatureRegistry>();
}
}  // namespace

TEST(registry_contains_five_features) {
    auto r = allFeatures();
    EXPECT_EQ(r->all().size(), size_t(5));
    for (const char* id : {"summarize", "proofread", "tone", "expand", "translate"}) EXPECT_TRUE(r->find(id) != nullptr);
    EXPECT_TRUE(r->find("nope") == nullptr);
}

TEST(registry_rejects_duplicates) {
    FeatureRegistry r;
    r.add(std::make_shared<SummarizeFeature>());
    EXPECT_THROWS(r.add(std::make_shared<SummarizeFeature>()), std::invalid_argument);
}

TEST(summarize_builds_prompt_with_defaults) {
    auto req = SummarizeFeature().buildRequest("Some long text.", {});
    EXPECT_EQ(req.task, std::string("summarize"));
    EXPECT_CONTAINS(req.userPrompt, "Length: medium");
    EXPECT_CONTAINS(req.userPrompt, "Format: paragraph");
    EXPECT_CONTAINS(req.userPrompt, "<text>\nSome long text.\n</text>");
    EXPECT_CONTAINS(req.systemPrompt, "never add information");
}

TEST(summarize_accepts_bullets_and_short) {
    auto req = SummarizeFeature().buildRequest("x", {{"length", "short"}, {"format", "bullets"}});
    EXPECT_CONTAINS(req.userPrompt, "Length: short");
    EXPECT_CONTAINS(req.userPrompt, "Format: bullets");
}

TEST(proofread_uses_dialect_and_explain) {
    auto req = ProofreadFeature().buildRequest("colour", {{"dialect", "uk"}, {"explain", "yes"}});
    EXPECT_CONTAINS(req.userPrompt, "British English");
    EXPECT_CONTAINS(req.systemPrompt, "bullet list of the changes");
}

TEST(tone_requires_tone_option) {
    EXPECT_THROWS(ToneFeature().buildRequest("hi", {}), ValidationError);
    auto req = ToneFeature().buildRequest("hi", {{"tone", "formal"}});
    EXPECT_CONTAINS(req.userPrompt, "Tone: formal");
}

TEST(tone_rejects_unknown_tone) {
    EXPECT_THROWS(ToneFeature().buildRequest("hi", {{"tone", "sarcastic"}}), ValidationError);
}

TEST(expand_accepts_free_text_audience) {
    auto req = ExpandFeature().buildRequest("notes", {{"audience", "new engineers"}});
    EXPECT_CONTAINS(req.userPrompt, "Audience: new engineers");
    EXPECT_CONTAINS(req.systemPrompt, "Do not invent");
}

TEST(translate_requires_language) {
    EXPECT_THROWS(TranslateFeature().buildRequest("hello", {}), ValidationError);
    auto req = TranslateFeature().buildRequest("hello", {{"lang", "Tamil"}, {"formality", "formal"}});
    EXPECT_CONTAINS(req.userPrompt, "Target language: Tamil");
    EXPECT_CONTAINS(req.userPrompt, "Formality: formal");
}

TEST(features_reject_empty_input_and_unknown_options) {
    for (const auto& f : allFeatures()->all()) {
        EXPECT_THROWS(f->buildRequest("   \n", {{"tone", "formal"}, {"lang", "French"}}), ValidationError);
        EXPECT_THROWS(f->buildRequest("text", {{"bogus", "1"}}), ValidationError);
    }
}

TEST(features_reject_oversized_input) {
    EXPECT_THROWS(SummarizeFeature().buildRequest(std::string(200001, 'a'), {}), ValidationError);
}

TEST(assistant_sends_feature_request_to_injected_client) {
    auto fake = std::make_shared<wt::RecordingLLMClient>();
    fake->reply = "Bonjour";
    WritingAssistant wa(fake, allFeatures());
    auto r = wa.run("translate", "Hello", {{"lang", "French"}});
    EXPECT_EQ(r.text, std::string("Bonjour"));
    EXPECT_EQ(fake->requests.size(), size_t(1));
    EXPECT_EQ(fake->requests[0].task, std::string("translate"));
    EXPECT_EQ(fake->requests[0].input, std::string("Hello"));
}

TEST(assistant_rejects_unknown_feature) {
    WritingAssistant wa(std::make_shared<wt::RecordingLLMClient>(), allFeatures());
    EXPECT_THROWS(wa.run("poem", "x"), ValidationError);
}

TEST(offline_proofread_fixes_common_errors) {
    EXPECT_EQ(OfflineClient::proofread("i  recieve teh mail .  it is   alot"),
              std::string("I receive the mail. It is a lot"));
}

TEST(offline_summarize_keeps_first_sentences) {
    EXPECT_EQ(OfflineClient::summarize("One. Two! Three? Four.", 2), std::string("One. Two!"));
}

TEST(offline_client_runs_every_feature) {
    ServiceContainer c;
    AppConfig cfg;
    cfg.provider = "offline";
    registerServices(c, cfg);
    auto wa = c.resolve<WritingAssistant>();
    EXPECT_EQ(wa->client().name(), std::string("offline"));
    EXPECT_EQ(wa->run("summarize", "A. B. C. D.", {{"length", "short"}}).text, std::string("A."));
    EXPECT_CONTAINS(wa->run("tone", "hi there", {{"tone", "formal"}}).text, "[formal tone]");
    EXPECT_CONTAINS(wa->run("translate", "hi", {{"lang", "French"}}).text, "[French]");
    EXPECT_CONTAINS(wa->run("expand", "idea", {}).text, "Idea");
    EXPECT_EQ(wa->run("proofread", "teh end", {}).text, std::string("The end"));
}
