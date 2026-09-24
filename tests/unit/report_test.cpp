#include "report/report.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "cdc/crossing.h"
#include "report/html_reporter.h"
#include "report/sarif_reporter.h"
#include "util/temp_file.h"

class ReportTest : public ::testing::Test {
   protected:
    opencdc::report::Reporter reporter;

    opencdc::cdc::Finding make_finding(const std::string& rule, const std::string& sev,
                                       const std::string& src, const std::string& dst) {
        opencdc::cdc::Finding f;
        f.rule_id = rule;
        f.rule_name = "test_rule";
        f.severity = sev;
        f.source_reg_name = src;
        f.dest_reg_name = dst;
        f.source_domain = "clk_a";
        f.dest_domain = "clk_b";
        f.reason = "test reason";
        f.source_loc = {"test.sv", 10, 5};
        return f;
    }
};

TEST_F(ReportTest, JsonEnvelopeFormat) {
    auto f1 = make_finding("CDC001", "error", "src", "dst");
    std::vector<opencdc::cdc::Finding> findings = {f1};

    std::ostringstream os;
    reporter.report_json(findings, os);

    std::string out = os.str();
    EXPECT_EQ(out.front(), '{');
    EXPECT_NE(out.find("\"analysis_status\""), std::string::npos);
    EXPECT_NE(out.find("\"finding_count\""), std::string::npos);
    EXPECT_NE(out.find("\"findings\""), std::string::npos);
    std::string needle = std::string("\"rule_id\": ") + "\"CDC001\"";
    EXPECT_NE(out.find(needle), std::string::npos);
}

TEST_F(ReportTest, JsonMultipleFindings) {
    auto f1 = make_finding("CDC001", "error", "src1", "dst1");
    auto f2 = make_finding("CDC003", "warning", "src2", "dst2");
    std::vector<opencdc::cdc::Finding> findings = {f1, f2};

    std::ostringstream os;
    reporter.report_json(findings, os);

    std::string out = os.str();
    EXPECT_NE(out.find("CDC001"), std::string::npos);
    EXPECT_NE(out.find("CDC003"), std::string::npos);
    size_t first_occur = out.find(',');
    EXPECT_NE(first_occur, std::string::npos);
}

TEST_F(ReportTest, TextFormat) {
    auto f = make_finding("CDC001", "error", "mod.src", "mod.dst");
    std::vector<opencdc::cdc::Finding> findings = {f};

    std::ostringstream os;
    reporter.report_text(findings, os);

    std::string out = os.str();
    EXPECT_NE(out.find("error [CDC001]"), std::string::npos);
    EXPECT_NE(out.find("mod.src"), std::string::npos);
    EXPECT_NE(out.find("-> mod.dst"), std::string::npos);
    EXPECT_NE(out.find("at test.sv:10"), std::string::npos);
}

TEST_F(ReportTest, TextWaivedFindingMarked) {
    auto f = make_finding("CDC001", "error", "src", "dst");
    f.waived = true;
    f.waiver_justification = "Reviewed safe";
    f.waiver_owner = "john";
    std::vector<opencdc::cdc::Finding> findings = {f};

    std::ostringstream os;
    reporter.report_text(findings, os);

    std::string out = os.str();
    EXPECT_NE(out.find("[WAIVED]"), std::string::npos);
    EXPECT_NE(out.find("Reviewed safe"), std::string::npos);
    EXPECT_NE(out.find("john"), std::string::npos);
}

TEST_F(ReportTest, SummaryLine) {
    auto f1 = make_finding("CDC001", "error", "src1", "dst1");
    auto f2 = make_finding("CDC003", "warning", "src2", "dst2");
    std::vector<opencdc::cdc::Finding> findings = {f1, f2};

    std::ostringstream os;
    reporter.report_summary(findings, os);

    std::string out = os.str();
    EXPECT_NE(out.find("analysis_status=complete"), std::string::npos);
    EXPECT_NE(out.find("findings=2"), std::string::npos);
    EXPECT_NE(out.find("errors=1"), std::string::npos);
    EXPECT_NE(out.find("warnings=1"), std::string::npos);
}

TEST_F(ReportTest, SummaryEmptyFindings) {
    std::vector<opencdc::cdc::Finding> findings;

    std::ostringstream os;
    reporter.report_summary(findings, os);

    std::string out = os.str();
    EXPECT_NE(out.find("analysis_status=complete"), std::string::npos);
    EXPECT_NE(out.find("findings=0"), std::string::npos);
}

TEST_F(ReportTest, HasUnsuppressedErrorsTrue) {
    auto f = make_finding("CDC001", "error", "src", "dst");
    std::vector<opencdc::cdc::Finding> findings = {f};
    EXPECT_TRUE(reporter.has_unsuppressed_errors(findings));
}

TEST_F(ReportTest, HasUnsuppressedErrorsAllWaived) {
    auto f = make_finding("CDC001", "error", "src", "dst");
    f.waived = true;
    std::vector<opencdc::cdc::Finding> findings = {f};
    EXPECT_FALSE(reporter.has_unsuppressed_errors(findings));
}

TEST_F(ReportTest, HasUnsuppressedErrorsOnlyWarnings) {
    auto f = make_finding("CDC003", "warning", "src", "dst");
    std::vector<opencdc::cdc::Finding> findings = {f};
    EXPECT_FALSE(reporter.has_unsuppressed_errors(findings));
}

TEST_F(ReportTest, HasUnsuppressedErrorsFalsePathSuppressed) {
    auto f = make_finding("CDC001", "error", "src", "dst");
    f.suppressed_by_false_path = true;
    std::vector<opencdc::cdc::Finding> findings = {f};
    EXPECT_FALSE(reporter.has_unsuppressed_errors(findings));
}

TEST_F(ReportTest, HasUnsuppressedErrorsMulticycleSuppressed) {
    auto f = make_finding("CDC001", "error", "src", "dst");
    f.suppressed_by_multicycle = true;
    std::vector<opencdc::cdc::Finding> findings = {f};
    EXPECT_FALSE(reporter.has_unsuppressed_errors(findings));
}

TEST_F(ReportTest, EscapeSpecialChars) {
    EXPECT_EQ(opencdc::report::Reporter::escape_json("hello"), "hello");
    EXPECT_EQ(opencdc::report::Reporter::escape_json("he\"llo"), "he\\\"llo");
    EXPECT_EQ(opencdc::report::Reporter::escape_json("he\\llo"), "he\\\\llo");
    EXPECT_EQ(opencdc::report::Reporter::escape_json("he\nllo"), "he\\nllo");
}

TEST_F(ReportTest, SummaryWithWaived) {
    auto f1 = make_finding("CDC001", "error", "src1", "dst1");
    f1.waived = true;
    auto f2 = make_finding("CDC001", "error", "src2", "dst2");
    std::vector<opencdc::cdc::Finding> findings = {f1, f2};

    std::ostringstream os;
    reporter.report_summary(findings, os);

    std::string out = os.str();
    EXPECT_NE(out.find("findings=2"), std::string::npos);
    EXPECT_NE(out.find("errors=1"), std::string::npos);
    EXPECT_NE(out.find("waived=1"), std::string::npos);
}

TEST(HtmlReportTest, CustomCssRemoteDirectivesStripped) {
    opencdc::cdc::Finding finding;
    finding.rule_id = "CDC001";
    finding.severity = "info";

    opencdc::report::HtmlReportOptions options;
    options.output_dir = opencdc::util::unique_temp_path("opencdc-html-css-test-");
    options.custom_css =
        ".ok { color: blue; }\n@import url(\"https://evil.example/x.css\");\n"
        ".bg { background: URL(https://evil.example/bg.png); }\n";
    opencdc::report::HtmlReporter reporter;
    reporter.generate_report({finding}, options);

    std::ifstream css(options.output_dir + "/style.css");
    std::string css_text((std::istreambuf_iterator<char>(css)), {});
    EXPECT_NE(css_text.find(".ok"), std::string::npos);
    EXPECT_EQ(css_text.find("@import"), std::string::npos);
    EXPECT_EQ(css_text.find("url("), std::string::npos);
    EXPECT_EQ(css_text.find("URL("), std::string::npos);
    std::error_code rm_ec;
    std::filesystem::remove_all(options.output_dir, rm_ec);
}

TEST(HtmlReportTest, EmptyOutputDirThrows) {
    opencdc::report::HtmlReportOptions options;
    options.output_dir = "";
    opencdc::report::HtmlReporter reporter;
    EXPECT_THROW(reporter.generate_report({}, options), std::runtime_error);
}

TEST(HtmlReportTest, OptionsAndFiltersAreRendered) {
    opencdc::cdc::Finding finding;
    finding.rule_id = "CDC001";
    finding.severity = "info";
    finding.source_reg_name = "src";
    finding.dest_reg_name = "dst";
    finding.source_loc.file = "source.sv";
    finding.source_loc.line = 7;

    opencdc::report::HtmlReportOptions options;
    options.output_dir = opencdc::util::unique_temp_path("opencdc-html-test-");
    options.include_source_snippets = false;
    options.dark_mode = true;
    options.custom_css = ".custom-test { color: red; }";
    opencdc::report::HtmlReporter reporter;
    reporter.generate_report({finding}, options);

    std::ifstream css(options.output_dir + "/style.css");
    std::string css_text((std::istreambuf_iterator<char>(css)), {});
    std::ifstream html(options.output_dir + "/findings.html");
    std::string html_text((std::istreambuf_iterator<char>(html)), {});
    EXPECT_NE(css_text.find(".custom-test"), std::string::npos);
    EXPECT_NE(css_text.find("Explicit dark mode"), std::string::npos);
    EXPECT_NE(html_text.find("id=\"rule-filter\""), std::string::npos);
    EXPECT_NE(html_text.find("value=\"info\""), std::string::npos);
    EXPECT_EQ(html_text.find("source.sv:7"), std::string::npos);
    std::error_code rm_ec;
    std::filesystem::remove_all(options.output_dir, rm_ec);
}

TEST_F(ReportTest, JsonIncludesSafetyFields) {
    auto f = make_finding("CDC001", "error", "src", "dst");
    f.safety_status = opencdc::cdc::SafetyStatus::VerifiedUnsafe;
    f.safety_provenance = "No synchronizer chain detected";
    f.is_gray_coded = true;
    f.has_handshake = false;
    f.source_module_path = "top.u_mod";
    f.dest_module_path = "top.u_dst";
    f.crosses_module_boundary = true;
    std::vector<opencdc::cdc::Finding> findings = {f};

    std::ostringstream os;
    reporter.report_json(findings, os);

    std::string out = os.str();
    EXPECT_NE(out.find("\"safety_status\": \"verified_unsafe\""), std::string::npos);
    EXPECT_NE(out.find("\"safety_provenance\""), std::string::npos);
    EXPECT_NE(out.find("\"is_gray_coded\": true"), std::string::npos);
    EXPECT_NE(out.find("\"has_handshake\": false"), std::string::npos);
    EXPECT_NE(out.find("\"source_module_path\": \"top.u_mod\""), std::string::npos);
    EXPECT_NE(out.find("\"dest_module_path\": \"top.u_dst\""), std::string::npos);
    EXPECT_NE(out.find("\"crosses_module_boundary\": true"), std::string::npos);
}

TEST_F(ReportTest, TextReportShowsAnalysisStatus) {
    auto f = make_finding("CDC001", "error", "src", "dst");
    std::vector<opencdc::cdc::Finding> findings = {f};

    std::ostringstream os;
    reporter.report_text(findings, os, "complete");

    std::string out = os.str();
    EXPECT_NE(out.find("Analysis status: complete"), std::string::npos);
}

TEST_F(ReportTest, SarifUriEncodedAndStartLineClamped) {
    auto f = make_finding("CDC001", "error", "src", "dst");
    f.source_loc.file = "/tmp/my dir/design.sv";
    f.source_loc.line = 0;
    opencdc::report::SarifReporter sarif;
    opencdc::analysis::CoverageResult coverage;
    opencdc::analysis::SignoffResult signoff;
    std::ostringstream os;
    sarif.report({f}, coverage, signoff, os);

    std::string out = os.str();
    EXPECT_NE(out.find("file:///tmp/my%20dir/design.sv"), std::string::npos);
    EXPECT_NE(out.find("\"startLine\": 1"), std::string::npos);
}

TEST(HtmlReportTest, SafetyFilterIsRendered) {
    opencdc::cdc::Finding finding;
    finding.rule_id = "CDC001";
    finding.severity = "error";
    finding.source_reg_name = "src";
    finding.dest_reg_name = "dst";
    finding.source_loc.file = "source.sv";
    finding.source_loc.line = 7;
    finding.safety_status = opencdc::cdc::SafetyStatus::VerifiedUnsafe;

    opencdc::report::HtmlReportOptions options;
    options.output_dir = opencdc::util::unique_temp_path("opencdc-html-safety-test-");
    options.include_source_snippets = false;
    opencdc::report::HtmlReporter reporter;
    reporter.generate_report({finding}, options);

    std::ifstream html(options.output_dir + "/findings.html");
    std::string html_text((std::istreambuf_iterator<char>(html)), {});
    EXPECT_NE(html_text.find("id=\"safety-filter\""), std::string::npos);
    EXPECT_NE(html_text.find("Verified Safe"), std::string::npos);
    EXPECT_NE(html_text.find("Verified Unsafe"), std::string::npos);
    std::error_code rm_ec;
    std::filesystem::remove_all(options.output_dir, rm_ec);
}
