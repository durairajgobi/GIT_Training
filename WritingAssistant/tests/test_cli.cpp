// Phase 3: end-to-end CLI runs with injected streams and services.
#include <sstream>

#include "fakes.hpp"
#include "test_framework.hpp"
#include "wa/cli.hpp"
#include "wa/json.hpp"

using namespace wa;

namespace {
struct Run {
    int code;
    std::string out, err;
};

Run run(std::vector<std::string> args, const std::string& stdinText = "", AppConfig cfg = {},
        std::shared_ptr<IHttpClient> http = nullptr) {
    if (cfg.provider == "auto") cfg.provider = "offline";
    ServiceContainer c;
    if (http) c.addInstance<IHttpClient>(http);
    std::istringstream in(stdinText);
    std::ostringstream out, err;
    int code = runCli(args, in, out, err, cfg, c);
    return {code, out.str(), err.str()};
}
}  // namespace

TEST(cli_parses_options_and_text) {
    auto a = parseArgs({"tone", "--tone", "formal", "hey", "there", "--model=claude-sonnet-5-5", "--stats"});
    EXPECT_EQ(a.command, std::string("tone"));
    EXPECT_EQ(a.text, std::string("hey there"));
    EXPECT_EQ(a.featureOptions.at("tone"), std::string("formal"));
    EXPECT_EQ(*a.model, std::string("claude-sonnet-5-5"));
    EXPECT_FALSE(a.featureOptions.count("model") > 0);
    EXPECT_TRUE(a.stats);
}

TEST(cli_parses_global_options) {
    auto a = parseArgs({"--provider", "anthropic", "--model", "m", "--effort", "high", "--no-fallbacks", "summarize"});
    AppConfig cfg;
    applyOverrides(a, cfg);
    EXPECT_EQ(cfg.provider, std::string("anthropic"));
    EXPECT_EQ(cfg.model, std::string("m"));
    EXPECT_EQ(cfg.effort, std::string("high"));
    EXPECT_FALSE(cfg.serverSideFallbacks);
}

TEST(cli_missing_option_value_is_error) {
    EXPECT_THROWS(parseArgs({"tone", "--tone"}), CliError);
    EXPECT_THROWS(parseArgs({}), CliError);
    EXPECT_THROWS(parseArgs({"summarize", "--stats=yes"}), CliError);
}

TEST(cli_help_and_version) {
    auto h = run({"--help"});
    EXPECT_EQ(h.code, 0);
    for (const char* f : {"summarize", "proofread", "tone", "expand", "translate"}) EXPECT_CONTAINS(h.out, f);
    EXPECT_EQ(run({"--version"}).out, std::string("1.0.0\n"));
}

TEST(cli_list_outputs_json) {
    auto r = run({"list"});
    EXPECT_EQ(r.code, 0);
    EXPECT_EQ(Json::parse(r.out).asArray().size(), size_t(5));
}

TEST(cli_reads_stdin_when_no_text_given) {
    auto r = run({"proofread"}, "teh cat sat .");
    EXPECT_EQ(r.code, 0);
    EXPECT_EQ(r.out, std::string("The cat sat.\n"));
}

TEST(cli_unknown_command_and_bad_option_exit_2) {
    EXPECT_EQ(run({"poem", "x"}).code, 2);
    auto r = run({"tone", "--tone", "angry", "hello"});
    EXPECT_EQ(r.code, 2);
    EXPECT_CONTAINS(r.err, "invalid value 'angry'");
    EXPECT_EQ(run({"summarize"}, "").code, 2);  // empty input
}

TEST(cli_missing_file_exit_2) {
    EXPECT_EQ(run({"summarize", "--file", "/no/such/file.txt"}).code, 2);
}

TEST(cli_end_to_end_with_fake_anthropic_api) {
    auto http = std::make_shared<wt::FakeHttpClient>();
    http->responses.push_back({200, R"({"model":"claude-opus-5-5","stop_reason":"end_turn",
        "content":[{"type":"text","text":"Vanakkam"}],"usage":{"input_tokens":30,"output_tokens":4}})"});
    AppConfig cfg;
    cfg.provider = "anthropic";
    cfg.apiKey = "sk-test";
    auto r = run({"translate", "--lang", "Tamil", "--stats", "Hello"}, "", cfg, http);
    EXPECT_EQ(r.code, 0);
    EXPECT_EQ(r.out, std::string("Vanakkam\n"));
    EXPECT_CONTAINS(r.err, "output_tokens=4");
    EXPECT_CONTAINS(Json::parse(http->requests.at(0).body).dump(), "Target language: Tamil");
}

TEST(cli_refusal_exit_3_and_api_error_exit_4) {
    AppConfig cfg;
    cfg.provider = "anthropic";
    cfg.apiKey = "sk-test";

    auto refusing = std::make_shared<wt::FakeHttpClient>();
    refusing->responses.push_back({200, R"({"content":[],"stop_reason":"refusal","stop_details":{"category":"bio"}})"});
    auto r = run({"summarize", "text"}, "", cfg, refusing);
    EXPECT_EQ(r.code, 3);
    EXPECT_CONTAINS(r.err, "category: bio");

    auto failing = std::make_shared<wt::FakeHttpClient>();
    failing->responses.push_back({401, R"({"error":{"type":"authentication_error","message":"invalid x-api-key"}})"});
    r = run({"summarize", "text"}, "", cfg, failing);
    EXPECT_EQ(r.code, 4);
    EXPECT_CONTAINS(r.err, "authentication_error");
}

TEST(cli_anthropic_without_key_is_clear_error) {
    AppConfig cfg;
    cfg.provider = "anthropic";
    auto r = run({"summarize", "text"}, "", cfg);
    EXPECT_EQ(r.code, 1);
    EXPECT_CONTAINS(r.err, "ANTHROPIC_API_KEY");
}
