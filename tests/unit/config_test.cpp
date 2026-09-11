#include "config/config.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <string>

#include "analysis/trend.h"

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
        "    severity: error\n");

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
        "    severity: banana\n",
        &error);

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
        "    severity: banana\n",
        &error);

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
        "    severity: warning\n");

    auto it = config.rules.find("CDC001");
    ASSERT_NE(it, config.rules.end());
    EXPECT_EQ(it->second.severity, "warning");
}

TEST(ConfigTest, FalsePathsParsed) {
    opencdc::config::ConfigParser parser;
    auto config = parser.parse_string(
        "false_paths:\n"
        "  - source: mod.src, dest: mod.dst\n"
        "  - source: mod.a, dest: mod.b\n");

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
        "  suppress_reset_crossings: true\n");

    EXPECT_TRUE(config.suppress_reset_crossings);
}

TEST(ConfigTest, BlackboxSectionParsed) {
    opencdc::config::ConfigParser parser;
    auto config = parser.parse_string(
        "blackboxes:\n"
        "  - module_name: xpm_cdc_gray\n"
        "    vendor: xilinx\n"
        "    is_safe_crossing: true\n"
        "    has_synchronizer: true\n"
        "    has_gray_encoding: true\n"
        "  - module_name: my_custom_sync\n"
        "    vendor: custom\n"
        "    is_safe_crossing: true\n");

    ASSERT_EQ(config.blackboxes.size(), 2u);
    EXPECT_EQ(config.blackboxes[0].module_name, "xpm_cdc_gray");
    EXPECT_EQ(config.blackboxes[0].vendor, "xilinx");
    EXPECT_TRUE(config.blackboxes[0].is_safe_crossing);
    EXPECT_TRUE(config.blackboxes[0].has_synchronizer);
    EXPECT_TRUE(config.blackboxes[0].has_gray_encoding);
    EXPECT_FALSE(config.blackboxes[0].has_async_fifo);
    EXPECT_EQ(config.blackboxes[1].module_name, "my_custom_sync");
    EXPECT_EQ(config.blackboxes[1].vendor, "custom");
    EXPECT_TRUE(config.blackboxes[1].is_safe_crossing);
}

TEST(ConfigTest, BlackboxSectionDefaults) {
    opencdc::config::ConfigParser parser;
    auto config = parser.parse_string(
        "blackboxes:\n"
        "  - module_name: test_bb\n");

    ASSERT_EQ(config.blackboxes.size(), 1u);
    EXPECT_EQ(config.blackboxes[0].module_name, "test_bb");
    EXPECT_TRUE(config.blackboxes[0].is_safe_crossing);   // default
    EXPECT_FALSE(config.blackboxes[0].has_synchronizer);  // default
}

TEST(ConfigTest, BlackboxEmpty) {
    opencdc::config::ConfigParser parser;
    auto config = parser.parse_string("");

    EXPECT_TRUE(config.blackboxes.empty());
}

TEST(ConfigTest, FalsePathsClockGroupsParsed) {
    opencdc::config::ConfigParser parser;
    auto config = parser.parse_string(
        "clock_groups:\n"
        "  group1:\n"
        "    clocks: clk_a, clk_b\n"
        "    exclusive: true\n");

    ASSERT_EQ(config.clock_groups.size(), 1u);
    EXPECT_EQ(config.clock_groups[0].clocks.size(), 2u);
    EXPECT_EQ(config.clock_groups[0].clocks[0], "clk_a");
    EXPECT_EQ(config.clock_groups[0].clocks[1], "clk_b");
    EXPECT_TRUE(config.clock_groups[0].exclusive);
}

TEST(ConfigTest, MulticyclePolicyParsed) {
    opencdc::config::ConfigParser parser;
    auto config = parser.parse_string(
        "multicycle_path_policy:\n"
        "  suppress_findings: true\n"
        "  suppress_rules: [CDC001, CDC002]\n");

    EXPECT_TRUE(config.multicycle_path_policy.suppress_findings);
    ASSERT_EQ(config.multicycle_path_policy.suppress_rules.size(), 2u);
    EXPECT_EQ(config.multicycle_path_policy.suppress_rules[0], "cdc001");
    EXPECT_EQ(config.multicycle_path_policy.suppress_rules[1], "cdc002");
}

TEST(ConfigTest, ResetPolicyParsed) {
    opencdc::config::ConfigParser parser;
    auto config = parser.parse_string(
        "reset_policy:\n"
        "  require_cdc_register_reset: true\n"
        "  check_same_clock_reset_crossings: true\n"
        "  detect_reset_synchronizer: false\n");

    EXPECT_TRUE(config.reset_policy.require_cdc_register_reset);
    EXPECT_TRUE(config.reset_policy.check_same_clock_reset_crossings);
    EXPECT_FALSE(config.reset_policy.detect_reset_synchronizer);
}

TEST(ConfigTest, OutputFormatSarifAccepted) {
    opencdc::config::ConfigParser parser;
    std::string error;
    auto config = parser.parse_string("output:\n  format: sarif\n", &error);
    EXPECT_TRUE(error.empty());
    EXPECT_EQ(config.output.format, "sarif");
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
