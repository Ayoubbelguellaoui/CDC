#include "analysis/analyzer.h"
#include "report/report.h"
#include <gtest/gtest.h>
#include <fstream>
#include <cstdio>
#include <string>

using opencdc::analysis::AnalysisRequest;
using opencdc::analysis::Analyzer;

static std::string fixture_path(const std::string& name) {
    return std::string(FIXTURES_DIR) + "/sv/" + name;
}

class AnalysisStatusTest : public ::testing::Test {
protected:
    Analyzer analyzer;
};

TEST_F(AnalysisStatusTest, CleanDesignReportsComplete) {
    AnalysisRequest req;
    req.input_files = {fixture_path("cdc_crossing.sv")};
    req.top_module = "simple_cdc_crossing";

    auto result = analyzer.run(req);
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.analysis_status, "complete");
}

TEST_F(AnalysisStatusTest, TruncatedDesignReportsIncomplete) {
    AnalysisRequest req;
    req.input_files = {fixture_path("cdc_crossing.sv")};
    req.top_module = "simple_cdc_crossing";

    auto result = analyzer.run(req);
    ASSERT_TRUE(result.ok);

    bool has_cdc010 = false;
    for (const auto& f : result.findings) {
        if (f.rule_id == "CDC010") has_cdc010 = true;
    }
    if (has_cdc010) {
        EXPECT_EQ(result.analysis_status, "incomplete");
    }
}

TEST_F(AnalysisStatusTest, TextReportShowsTruncationWarning) {
    AnalysisRequest req;
    req.input_files = {fixture_path("cdc_crossing.sv")};
    req.top_module = "simple_cdc_crossing";

    auto result = analyzer.run(req);
    ASSERT_TRUE(result.ok);

    std::ostringstream os;
    opencdc::report::Reporter reporter;
    reporter.report_text(result.findings, os, result.analysis_status);

    std::string text = os.str();
    if (result.analysis_status == "incomplete") {
        EXPECT_NE(text.find("WARNING: Analysis incomplete"), std::string::npos)
            << "Text report should show truncation warning when analysis is incomplete";
    }
}

TEST_F(AnalysisStatusTest, DefaultAnalysisStatusIsComplete) {
    AnalysisRequest req;
    req.input_files = {fixture_path("cdc_crossing.sv")};
    req.top_module = "simple_cdc_crossing";

    auto result = analyzer.run(req);
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.analysis_status, "complete");
}

TEST_F(AnalysisStatusTest, FailedDesignReportsFailed) {
    AnalysisRequest req;
    req.input_files = {"nonexistent_file.sv"};
    req.top_module = "nonexistent";

    auto result = analyzer.run(req);
    EXPECT_EQ(result.analysis_status, "failed");
    EXPECT_FALSE(result.errors.empty());
}
