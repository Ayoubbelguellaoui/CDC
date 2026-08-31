#include "analysis/analyzer.h"
#include <gtest/gtest.h>
#include <fstream>
#include <cstdio>
#include <string>

using opencdc::analysis::AnalysisRequest;
using opencdc::analysis::Analyzer;

static std::string fixture_path(const std::string& name) {
    return std::string(FIXTURES_DIR) + "/sv/" + name;
}

class AnalyzerTest : public ::testing::Test {
protected:
    Analyzer analyzer;
};

TEST_F(AnalyzerTest, UnsynchronizedCrossingReported) {
    AnalysisRequest req;
    req.input_files = {fixture_path("cdc_crossing.sv")};
    req.top_module = "simple_cdc_crossing";

    auto result = analyzer.run(req);
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.errors.size(), 0u);

    bool has_cdc001 = false;
    for (const auto& f : result.findings) {
        if (f.rule_id == "CDC001") has_cdc001 = true;
    }
    EXPECT_TRUE(has_cdc001);
}

TEST_F(AnalyzerTest, Clean2ffSynchronizerHasNoError) {
    AnalysisRequest req;
    req.input_files = {fixture_path("sync_2ff.sv")};
    req.top_module = "sync_2ff";

    auto result = analyzer.run(req);
    ASSERT_TRUE(result.ok);

    for (const auto& f : result.findings) {
        EXPECT_NE(f.severity, "error")
            << "unexpected error finding: " << f.rule_id << " " << f.reason;
    }
}

// P1 acceptance: config-file false paths must be honored even when no
// constraints file is passed (the pre-P1 bug attached constraints only
// when --constraints was present).
TEST_F(AnalyzerTest, ConfigOnlyFalsePathSuppressesCrossing) {
    const std::string cfg_path = "/tmp/opencdc_analyzer_cfg.yaml";
    {
        std::ofstream cfg(cfg_path);
        cfg << "false_paths:\n"
            << "  - source: src_ff, dest: dst_ff\n";
    }

    AnalysisRequest req;
    req.input_files = {fixture_path("cdc_crossing.sv")};
    req.top_module = "simple_cdc_crossing";
    req.config_path = cfg_path;

    auto result = analyzer.run(req);
    std::remove(cfg_path.c_str());
    ASSERT_TRUE(result.ok);

    for (const auto& f : result.findings) {
        EXPECT_NE(f.rule_id, "CDC001")
            << "config-only false path did not suppress crossing";
    }
}

TEST_F(AnalyzerTest, CliFalsePathSuppressesCrossing) {
    AnalysisRequest req;
    req.input_files = {fixture_path("cdc_crossing.sv")};
    req.top_module = "simple_cdc_crossing";
    req.false_paths = {{"src_ff", "dst_ff"}};

    auto result = analyzer.run(req);
    ASSERT_TRUE(result.ok);

    for (const auto& f : result.findings) {
        EXPECT_NE(f.rule_id, "CDC001")
            << "request false path did not suppress crossing";
    }
}

TEST_F(AnalyzerTest, UnknownRuleIsInputError) {
    AnalysisRequest req;
    req.input_files = {fixture_path("cdc_crossing.sv")};
    req.top_module = "simple_cdc_crossing";
    req.disable_rules = {"CDC999"};

    auto result = analyzer.run(req);
    EXPECT_FALSE(result.ok);
    ASSERT_FALSE(result.errors.empty());
    EXPECT_NE(result.errors[0].find("unknown rule"), std::string::npos);
}

TEST_F(AnalyzerTest, InvalidSeverityOverrideIsInputError) {
    AnalysisRequest req;
    req.input_files = {fixture_path("cdc_crossing.sv")};
    req.top_module = "simple_cdc_crossing";
    req.severity_overrides = {"CDC001=banana"};

    auto result = analyzer.run(req);
    EXPECT_FALSE(result.ok);
    ASSERT_FALSE(result.errors.empty());
}

TEST_F(AnalyzerTest, MissingTopModuleIsInputError) {
    AnalysisRequest req;
    req.input_files = {fixture_path("cdc_crossing.sv")};
    req.top_module = "no_such_module";

    auto result = analyzer.run(req);
    EXPECT_FALSE(result.ok);
    ASSERT_FALSE(result.errors.empty());
}
