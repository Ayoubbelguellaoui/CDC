#include <gtest/gtest.h>

#include "analysis/coverage.h"
#include "analysis/signoff.h"
#include "cdc/crossing.h"
#include "config/config.h"

using namespace opencdc::cdc;
using namespace opencdc::analysis;
using namespace opencdc::config;

static Finding make_finding(const std::string& rule_id, const std::string& severity,
                            const std::string& src_domain, const std::string& dst_domain,
                            SafetyStatus status = SafetyStatus::Unknown) {
    Finding f;
    f.rule_id = rule_id;
    f.severity = severity;
    f.source_domain = src_domain;
    f.dest_domain = dst_domain;
    f.safety_status = status;
    return f;
}

class CoverageTest : public ::testing::Test {
   protected:
    CoverageEngine engine;
};

TEST_F(CoverageTest, EmptyFindings) {
    auto result = engine.compute({}, "complete");
    EXPECT_EQ(result.counts.total, 0u);
    EXPECT_EQ(result.counts.errors, 0u);
    EXPECT_EQ(result.counts.warnings, 0u);
}

TEST_F(CoverageTest, CountsBySeverity) {
    std::vector<Finding> findings;
    findings.push_back(make_finding("CDC001", "error", "clk_a", "clk_b"));
    findings.push_back(make_finding("CDC001", "error", "clk_a", "clk_b"));
    findings.push_back(make_finding("CDC003", "warning", "clk_a", "clk_b"));
    findings.push_back(make_finding("CDC007", "info", "clk_a", "clk_b"));

    auto result = engine.compute(findings, "complete");
    EXPECT_EQ(result.counts.total, 4u);
    EXPECT_EQ(result.counts.errors, 2u);
    EXPECT_EQ(result.counts.warnings, 1u);
    EXPECT_EQ(result.counts.info, 1u);
}

TEST_F(CoverageTest, CountsBySafetyStatus) {
    std::vector<Finding> findings;
    Finding f1 = make_finding("CDC001", "error", "clk_a", "clk_b", SafetyStatus::VerifiedUnsafe);
    Finding f2 = make_finding("CDC001", "warning", "clk_a", "clk_b", SafetyStatus::VerifiedSafe);
    Finding f3 = make_finding("CDC003", "warning", "clk_a", "clk_b", SafetyStatus::Candidate);
    findings.push_back(f1);
    findings.push_back(f2);
    findings.push_back(f3);

    auto result = engine.compute(findings, "complete");
    EXPECT_EQ(result.counts.verified_unsafe, 1u);
    EXPECT_EQ(result.counts.verified_safe, 1u);
    EXPECT_EQ(result.counts.candidate, 1u);
}

TEST_F(CoverageTest, CountsWaivedAndSuppressed) {
    std::vector<Finding> findings;
    Finding f1 = make_finding("CDC001", "error", "clk_a", "clk_b");
    f1.waived = true;
    Finding f2 = make_finding("CDC001", "info", "clk_a", "clk_b");
    f2.suppressed_by_false_path = true;
    Finding f3 = make_finding("CDC001", "info", "clk_a", "clk_b");
    f3.suppressed_by_multicycle = true;
    findings.push_back(f1);
    findings.push_back(f2);
    findings.push_back(f3);

    auto result = engine.compute(findings, "complete");
    EXPECT_EQ(result.counts.waived, 1u);
    EXPECT_EQ(result.counts.suppressed, 1u);
    EXPECT_EQ(result.counts.multicycle_suppressed, 1u);
}

TEST_F(CoverageTest, ClockPairBreakdown) {
    std::vector<Finding> findings;
    findings.push_back(make_finding("CDC001", "error", "clk_a", "clk_b"));
    findings.push_back(make_finding("CDC001", "error", "clk_a", "clk_b"));
    findings.push_back(make_finding("CDC001", "warning", "clk_c", "clk_d"));

    auto result = engine.compute(findings, "complete");
    ASSERT_EQ(result.clock_pairs.size(), 2u);

    // First clock pair: clk_a→clk_b
    EXPECT_EQ(result.clock_pairs[0].source_domain, "clk_a");
    EXPECT_EQ(result.clock_pairs[0].dest_domain, "clk_b");
    EXPECT_EQ(result.clock_pairs[0].crossing_count, 2u);
    EXPECT_EQ(result.clock_pairs[0].error_count, 2u);

    // Second clock pair: clk_c→clk_d
    EXPECT_EQ(result.clock_pairs[1].source_domain, "clk_c");
    EXPECT_EQ(result.clock_pairs[1].dest_domain, "clk_d");
    EXPECT_EQ(result.clock_pairs[1].crossing_count, 1u);
    EXPECT_EQ(result.clock_pairs[1].warning_count, 1u);
}

TEST_F(CoverageTest, RuleBreakdown) {
    std::vector<Finding> findings;
    findings.push_back(make_finding("CDC001", "error", "clk_a", "clk_b"));
    findings.push_back(make_finding("CDC001", "error", "clk_a", "clk_b"));
    findings.push_back(make_finding("CDC003", "warning", "clk_a", "clk_b"));

    auto result = engine.compute(findings, "complete");
    ASSERT_EQ(result.rules.size(), 2u);
    EXPECT_EQ(result.rules[0].rule_id, "CDC001");
    EXPECT_EQ(result.rules[0].count, 2u);
    EXPECT_EQ(result.rules[1].rule_id, "CDC003");
    EXPECT_EQ(result.rules[1].count, 1u);
}

TEST_F(CoverageTest, TruncatedPath) {
    std::vector<Finding> findings;
    findings.push_back(make_finding("CDC010", "warning", "", ""));

    auto result = engine.compute(findings, "complete");
    EXPECT_EQ(result.counts.truncated, 1u);
    EXPECT_EQ(result.counts.analyzed, 0u);
}

class SignoffTest : public ::testing::Test {
   protected:
    SignoffEngine engine;
};

TEST_F(SignoffTest, CleanPass) {
    std::vector<Finding> findings;
    findings.push_back(make_finding("CDC001", "warning", "clk_a", "clk_b"));

    auto result = engine.evaluate(findings, "complete");
    EXPECT_EQ(result.status, SignoffStatus::Pass);
    EXPECT_EQ(signoff_status_name(result.status), "PASS");
}

TEST_F(SignoffTest, FailOnErrors) {
    std::vector<Finding> findings;
    findings.push_back(make_finding("CDC001", "error", "clk_a", "clk_b"));

    auto result = engine.evaluate(findings, "complete");
    EXPECT_EQ(result.status, SignoffStatus::Fail);
    EXPECT_EQ(signoff_status_name(result.status), "FAIL");
}

TEST_F(SignoffTest, PassWithWaivers) {
    std::vector<Finding> findings;
    Finding f = make_finding("CDC001", "error", "clk_a", "clk_b");
    f.waived = true;
    findings.push_back(f);

    auto result = engine.evaluate(findings, "complete");
    EXPECT_EQ(result.status, SignoffStatus::PassWithWaivers);
    EXPECT_EQ(signoff_status_name(result.status), "PASS_WITH_WAIVERS");
}

TEST_F(SignoffTest, Incomplete) {
    std::vector<Finding> findings;
    findings.push_back(make_finding("CDC010", "warning", "", ""));

    auto result = engine.evaluate(findings, "incomplete");
    EXPECT_EQ(result.status, SignoffStatus::Incomplete);
    EXPECT_EQ(signoff_status_name(result.status), "INCOMPLETE");
}

TEST_F(SignoffTest, ErrorStatus) {
    std::vector<Finding> findings;

    auto result = engine.evaluate(findings, "failed");
    EXPECT_EQ(result.status, SignoffStatus::Error);
    EXPECT_EQ(signoff_status_name(result.status), "ERROR");
}

TEST_F(SignoffTest, CoverageComputed) {
    std::vector<Finding> findings;
    findings.push_back(make_finding("CDC001", "error", "clk_a", "clk_b"));
    findings.push_back(make_finding("CDC003", "warning", "clk_a", "clk_b"));

    auto result = engine.evaluate(findings, "complete");
    EXPECT_EQ(result.coverage.counts.total, 2u);
    EXPECT_EQ(result.coverage.counts.errors, 1u);
    EXPECT_EQ(result.coverage.counts.warnings, 1u);
}

TEST_F(SignoffTest, ErrorsButSomeWaivedStillFail) {
    std::vector<Finding> findings;
    Finding waived = make_finding("CDC001", "error", "clk_a", "clk_b");
    waived.waived = true;
    findings.push_back(waived);
    findings.push_back(make_finding("CDC001", "error", "clk_a", "clk_b"));

    auto result = engine.evaluate(findings, "complete");
    EXPECT_EQ(result.status, SignoffStatus::Fail);
}

TEST_F(SignoffTest, AsicSignoffWarningsFail) {
    std::vector<Finding> findings;
    findings.push_back(make_finding("CDC001", "warning", "clk_a", "clk_b"));

    Config cfg;
    auto result = engine.evaluate(findings, "complete", cfg, "asic_signoff");
    EXPECT_EQ(result.status, SignoffStatus::Fail);
    EXPECT_GT(result.warnings_as_errors, 0u);
    EXPECT_EQ(result.methodology, "asic_signoff");
}

TEST_F(SignoffTest, FpgaWarningsPass) {
    std::vector<Finding> findings;
    findings.push_back(make_finding("CDC001", "warning", "clk_a", "clk_b"));

    Config cfg;
    auto result = engine.evaluate(findings, "complete", cfg, "fpga");
    EXPECT_EQ(result.status, SignoffStatus::Pass);
    EXPECT_EQ(result.warnings_as_errors, 0u);
}

TEST_F(SignoffTest, StrictNonCriticalWarningsPass) {
    std::vector<Finding> findings;
    // CDC003 is not in the critical rules list for warnings-as-errors.
    findings.push_back(make_finding("CDC003", "warning", "clk_a", "clk_b"));

    Config cfg;
    auto result = engine.evaluate(findings, "complete", cfg, "strict");
    EXPECT_EQ(result.status, SignoffStatus::Pass);
}

TEST_F(SignoffTest, AsicSignoffCriticalWarningsFail) {
    std::vector<Finding> findings;
    findings.push_back(make_finding("CDC001", "warning", "clk_a", "clk_b"));
    findings.push_back(make_finding("CDC004", "warning", "clk_a", "clk_b"));

    Config cfg;
    auto result = engine.evaluate(findings, "complete", cfg, "asic_signoff");
    EXPECT_EQ(result.status, SignoffStatus::Fail);
    EXPECT_EQ(result.warnings_as_errors, 2u);
}

TEST_F(SignoffTest, AsicSignoffWaivedWarningsPass) {
    std::vector<Finding> findings;
    Finding w1 = make_finding("CDC001", "warning", "clk_a", "clk_b");
    w1.waived = true;
    findings.push_back(w1);

    Config cfg;
    auto result = engine.evaluate(findings, "complete", cfg, "asic_signoff");
    EXPECT_EQ(result.status, SignoffStatus::PassWithWaivers);
    EXPECT_EQ(result.warnings_as_errors, 0u);
}

TEST_F(SignoffTest, AsicSignoffVerifiedSafeInfoPasses) {
    std::vector<Finding> findings;
    findings.push_back(make_finding("CDC001", "info", "clk_a", "clk_b", SafetyStatus::VerifiedSafe));

    Config cfg;
    auto result = engine.evaluate(findings, "complete", cfg, "asic_signoff");
    EXPECT_EQ(result.status, SignoffStatus::Pass);
    EXPECT_EQ(result.warnings_as_errors, 0u);
}

TEST_F(SignoffTest, AsicSignoffVerifiedSafeWarningSkipped) {
    std::vector<Finding> findings;
    findings.push_back(
        make_finding("CDC001", "warning", "clk_a", "clk_b", SafetyStatus::VerifiedSafe));

    Config cfg;
    auto result = engine.evaluate(findings, "complete", cfg, "asic_signoff");
    EXPECT_EQ(result.status, SignoffStatus::Pass);
    EXPECT_EQ(result.warnings_as_errors, 0u);
}

TEST_F(SignoffTest, AsicSignoffAmbiguousWarningFails) {
    std::vector<Finding> findings;
    findings.push_back(
        make_finding("CDC001", "warning", "clk_a", "clk_b", SafetyStatus::Ambiguous));

    Config cfg;
    auto result = engine.evaluate(findings, "complete", cfg, "asic_signoff");
    EXPECT_EQ(result.status, SignoffStatus::Fail);
    EXPECT_GT(result.warnings_as_errors, 0u);
}

TEST_F(SignoffTest, DefaultMethodologyNoWarningFail) {
    std::vector<Finding> findings;
    findings.push_back(make_finding("CDC001", "warning", "clk_a", "clk_b"));

    Config cfg;
    auto result = engine.evaluate(findings, "complete", cfg, "default");
    EXPECT_EQ(result.status, SignoffStatus::Pass);
    EXPECT_EQ(result.warnings_as_errors, 0u);
}
