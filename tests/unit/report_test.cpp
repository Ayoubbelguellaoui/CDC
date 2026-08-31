#include "report/report.h"
#include "report/html_reporter.h"
#include "cdc/crossing.h"
#include <gtest/gtest.h>
#include <sstream>
#include <fstream>
#include <cstdio>

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

TEST_F(ReportTest, JsonArrayFormat) {
    auto f1 = make_finding("CDC001", "error", "src", "dst");
    std::vector<opencdc::cdc::Finding> findings = {f1};

    std::ostringstream os;
    reporter.report_json(findings, os);

    std::string out = os.str();
    EXPECT_EQ(out.front(), '[');
    EXPECT_TRUE(out.back() == ']' || out.back() == '\n');
    EXPECT_NE(out.find("]"), std::string::npos);
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
    EXPECT_NE(out.find("2 finding(s)"), std::string::npos);
    EXPECT_NE(out.find("1 error(s)"), std::string::npos);
    EXPECT_NE(out.find("1 warning(s)"), std::string::npos);
}

TEST_F(ReportTest, SummaryEmptyFindings) {
    std::vector<opencdc::cdc::Finding> findings;

    std::ostringstream os;
    reporter.report_summary(findings, os);

    EXPECT_EQ(os.str(), "No CDC findings.\n");
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
    EXPECT_NE(out.find("2 finding(s)"), std::string::npos);
    EXPECT_NE(out.find("1 error(s)"), std::string::npos);
    EXPECT_NE(out.find("1 waived"), std::string::npos);
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
    options.output_dir = "/tmp/opencdc-html-test";
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
    std::remove((options.output_dir + "/index.html").c_str());
    std::remove((options.output_dir + "/findings.html").c_str());
    std::remove((options.output_dir + "/style.css").c_str());
    std::remove((options.output_dir + "/script.js").c_str());
    std::remove(options.output_dir.c_str());
}
