#include "config/config.h"
#include "analysis/trend.h"
#include <gtest/gtest.h>
#include <string>
#include <cstdio>

static std::string fixture_path(const std::string& name) {
    return std::string(FIXTURES_DIR) + "/config/" + name;
}

TEST(ConfigTest, ParseBasicConfig) {
    opencdc::config::ConfigParser parser;
    auto config = parser.parse_file(fixture_path("basic.yaml"));

    EXPECT_EQ(config.rules.size(), 3u);

    auto it1 = config.rules.find("CDC001");
    ASSERT_NE(it1, config.rules.end());
    EXPECT_TRUE(it1->second.enabled);
    EXPECT_EQ(it1->second.severity, "error");

    auto it3 = config.rules.find("CDC003");
    ASSERT_NE(it3, config.rules.end());
    EXPECT_FALSE(it3->second.enabled);

    auto it7 = config.rules.find("CDC007");
    ASSERT_NE(it7, config.rules.end());
    EXPECT_EQ(it7->second.severity, "warning");

    EXPECT_EQ(config.waivers.size(), 1u);
    EXPECT_EQ(config.waivers[0].rule_id, "CDC001");
    EXPECT_EQ(config.waivers[0].source_reg, "mod.src");
    EXPECT_EQ(config.waivers[0].dest_reg, "mod.dst");
    EXPECT_EQ(config.waivers[0].justification, "Known safe crossing");
    EXPECT_EQ(config.waivers[0].owner, "@team");
    EXPECT_EQ(config.waivers[0].expiry, "2027-12-31");

    EXPECT_EQ(config.output.format, "json");
    EXPECT_EQ(config.output.file, "report.json");
}

TEST(ConfigTest, ParseEmptyConfig) {
    opencdc::config::ConfigParser parser;
    auto config = parser.parse_string("");

    EXPECT_TRUE(config.rules.empty());
    EXPECT_TRUE(config.waivers.empty());
    EXPECT_TRUE(config.output.format.empty());
    EXPECT_TRUE(config.output.file.empty());
}

TEST(ConfigTest, ParseCommentsIgnored) {
    opencdc::config::ConfigParser parser;
    auto config = parser.parse_string(
        "# This is a comment\n"
        "rules:\n"
        "  # Rule comment\n"
        "  CDC001:\n"
        "    enabled: true\n"
        "    # severity comment\n"
        "    severity: error\n"
    );

    EXPECT_EQ(config.rules.size(), 1u);
    auto it = config.rules.find("CDC001");
    ASSERT_NE(it, config.rules.end());
    EXPECT_TRUE(it->second.enabled);
    EXPECT_EQ(it->second.severity, "error");
}

TEST(ConfigTest, NonexistentFileReturnsEmpty) {
    opencdc::config::ConfigParser parser;
    std::string error;
    auto config = parser.parse_file("/nonexistent/path.yaml", &error);

    EXPECT_TRUE(config.rules.empty());
    EXPECT_TRUE(config.waivers.empty());
    EXPECT_FALSE(error.empty());
}

TEST(ConfigTest, MalformedSeverityIgnored) {
    opencdc::config::ConfigParser parser;
    std::string error;
    auto config = parser.parse_string(
        "rules:\n"
        "  CDC001:\n"
        "    severity: banana\n"
    , &error);

    EXPECT_TRUE(config.rules.empty());
    EXPECT_NE(error.find("Invalid value"), std::string::npos);
}

TEST(ConfigTest, MalformedSeverityWithValidField) {
    opencdc::config::ConfigParser parser;
    std::string error;
    auto config = parser.parse_string(
        "rules:\n"
        "  CDC001:\n"
        "    enabled: true\n"
        "    severity: banana\n", &error);

    EXPECT_TRUE(config.rules.empty());
    EXPECT_FALSE(error.empty());
}

TEST(ConfigTest, InvalidBooleanReportsError) {
    opencdc::config::ConfigParser parser;
    std::string error;
    parser.parse_string("output:\n  suppress_reset_crossings: maybe\n", &error);
    EXPECT_NE(error.find("Invalid value"), std::string::npos);
}

TEST(ConfigTest, ValidSeverityAccepted) {
    opencdc::config::ConfigParser parser;
    auto config = parser.parse_string(
        "rules:\n"
        "  CDC001:\n"
        "    severity: warning\n"
    );

    auto it = config.rules.find("CDC001");
    ASSERT_NE(it, config.rules.end());
    EXPECT_EQ(it->second.severity, "warning");
}

TEST(ConfigTest, FalsePathsParsed) {
    opencdc::config::ConfigParser parser;
    auto config = parser.parse_string(
        "false_paths:\n"
        "  - source: mod.src, dest: mod.dst\n"
        "  - source: mod.a, dest: mod.b\n"
    );

    ASSERT_EQ(config.false_paths.size(), 2u);
    EXPECT_EQ(config.false_paths[0].source_reg, "mod.src");
    EXPECT_EQ(config.false_paths[0].dest_reg, "mod.dst");
    EXPECT_EQ(config.false_paths[1].source_reg, "mod.a");
    EXPECT_EQ(config.false_paths[1].dest_reg, "mod.b");
}

TEST(ConfigTest, SuppressResetCrossingsParsed) {
    opencdc::config::ConfigParser parser;
    auto config = parser.parse_string(
        "output:\n"
        "  suppress_reset_crossings: true\n"
    );

    EXPECT_TRUE(config.suppress_reset_crossings);
}

TEST(TrendTest, BaselineRoundTripsDelimitersNewlinesAndDuplicates) {
    opencdc::cdc::Finding finding;
    finding.rule_id = "CDC:001";
    finding.rule_name = "name\nwith newline";
    finding.severity = "error";
    finding.source_reg_name = "src:one";
    finding.dest_reg_name = "dst\ntwo";
    finding.source_domain = "clk:a";
    finding.dest_domain = "clk:b";
    finding.reason = "reason\nwith:delimiter";
    finding.source_loc.file = "file:name.sv";
    finding.source_loc.line = 42;
    finding.waived = true;
    finding.waiver_justification = "why\n";

    const std::string path = "/tmp/opencdc-trend-test.baseline";
    opencdc::analysis::TrendAnalyzer analyzer;
    analyzer.save_baseline("baseline:name\n", {finding, finding}, path);
    auto baseline = analyzer.load_baseline(path);

    ASSERT_EQ(baseline.findings.size(), 2u);
    EXPECT_EQ(baseline.name, "baseline:name\n");
    EXPECT_EQ(baseline.findings[0].reason, finding.reason);
    auto report = analyzer.compare(baseline, {finding, finding});
    EXPECT_EQ(report.persistent_findings, 2);
    EXPECT_EQ(report.new_findings, 0);
    EXPECT_EQ(report.fixed_findings, 0);
    std::remove(path.c_str());
}
