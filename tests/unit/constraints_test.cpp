#include "clock/constraints.h"
#include <gtest/gtest.h>

using namespace opencdc::clock;

class ConstraintsTest : public ::testing::Test {
protected:
    ConstraintsParser parser;
};

TEST_F(ConstraintsTest, ParseYamlClocks) {
    std::string yaml = R"(
clocks:
  clk_core:
    frequency: 100
    source: pll
  clk_periph:
    frequency: 50
    divider: 2
    master_clock: clk_core
)";
    
    auto constraints = parser.parse_yaml(yaml);
    
    EXPECT_EQ(constraints.clocks.size(), 2u);
    EXPECT_EQ(constraints.clocks[0].name, "clk_core");
    EXPECT_EQ(constraints.clocks[0].frequency_mhz, 100.0);
    EXPECT_EQ(constraints.clocks[1].name, "clk_periph");
    EXPECT_TRUE(constraints.clocks[1].is_generated);
    EXPECT_EQ(constraints.clocks[1].divider_ratio, 2.0);
}

TEST_F(ConstraintsTest, ParseYamlFalsePaths) {
    std::string yaml = R"(
false_paths:
  - from_clock: clk_core, to_clock: clk_test, reason: "Test clock"
  - from_reg: top.test.src, to_reg: top.test.dst
)";
    
    auto constraints = parser.parse_yaml(yaml);
    
    EXPECT_EQ(constraints.false_paths.size(), 2u);
    EXPECT_EQ(constraints.false_paths[0].from_clock, "clk_core");
    EXPECT_EQ(constraints.false_paths[0].to_clock, "clk_test");
    EXPECT_EQ(constraints.false_paths[1].from_reg, "top.test.src");
}

TEST_F(ConstraintsTest, ParseYamlClockGroups) {
    std::string yaml = R"(
clock_groups:
  async_group:
    clocks: clk_core, clk_periph, clk_test
    asynchronous: true
)";
    
    auto constraints = parser.parse_yaml(yaml);
    
    EXPECT_EQ(constraints.clock_groups.size(), 1u);
    EXPECT_EQ(constraints.clock_groups[0].name, "async_group");
    EXPECT_TRUE(constraints.clock_groups[0].asynchronous);
    EXPECT_EQ(constraints.clock_groups[0].clocks.size(), 3u);
}

TEST_F(ConstraintsTest, IsFalsePath) {
    ClockConstraints constraints;
    
    FalsePath fp;
    fp.from_clock = "clk_core";
    fp.to_clock = "clk_test";
    constraints.false_paths.push_back(fp);
    
    EXPECT_TRUE(constraints.is_false_path("clk_core", "clk_test"));
    EXPECT_FALSE(constraints.is_false_path("clk_core", "clk_periph"));
}

TEST_F(ConstraintsTest, IsAsynchronous) {
    ClockConstraints constraints;
    
    ClockGroup group1;
    group1.name = "group_a";
    group1.clocks = {"clk_core"};
    group1.asynchronous = true;
    group1.set_id = 0;
    constraints.clock_groups.push_back(group1);
    
    ClockGroup group2;
    group2.name = "group_b";
    group2.clocks = {"clk_periph"};
    group2.asynchronous = true;
    group2.set_id = 0;
    constraints.clock_groups.push_back(group2);
    
    EXPECT_TRUE(constraints.is_asynchronous("clk_core", "clk_periph"));
    EXPECT_FALSE(constraints.is_asynchronous("clk_core", "clk_test"));
}

class SdcReaderTest : public ::testing::Test {
protected:
    SdcReader reader;
};

TEST_F(SdcReaderTest, ParseCreateClock) {
    std::string sdc = R"(
create_clock -name clk_core -period 10.0 [get_ports clk_core]
create_clock -name clk_periph -period 20.0
)";
    
    auto constraints = reader.parse_sdc_content(sdc);
    
    EXPECT_EQ(constraints.clocks.size(), 2u);
    EXPECT_EQ(constraints.clocks[0].name, "clk_core");
    EXPECT_EQ(constraints.clocks[0].period_ns, 10.0);
    EXPECT_EQ(constraints.clocks[1].name, "clk_periph");
}

TEST_F(SdcReaderTest, ParseGeneratedClock) {
    std::string sdc = R"(
create_generated_clock -name clk_div2 -master_clock clk_core -divide_by 2 [get_pins pll/clk_out]
)";
    
    auto constraints = reader.parse_sdc_content(sdc);
    
    EXPECT_EQ(constraints.clocks.size(), 1u);
    EXPECT_TRUE(constraints.clocks[0].is_generated);
    EXPECT_EQ(constraints.clocks[0].master_clock, "clk_core");
    EXPECT_EQ(constraints.clocks[0].divider_ratio, 2.0);
}

TEST_F(SdcReaderTest, ParseFalsePath) {
    std::string sdc = R"(
set_false_path -from [get_clocks clk_core] -to [get_clocks clk_test]
)";
    
    auto constraints = reader.parse_sdc_content(sdc);
    
    EXPECT_EQ(constraints.false_paths.size(), 1u);
    EXPECT_EQ(constraints.false_paths[0].from_clock, "clk_core");
    EXPECT_EQ(constraints.false_paths[0].to_clock, "clk_test");
}

TEST_F(SdcReaderTest, ParseClockGroups) {
    std::string sdc = R"(
set_clock_groups -asynchronous -group [get_clocks clk_core] -group [get_clocks clk_periph]
)";
    
    auto constraints = reader.parse_sdc_content(sdc);
    
    EXPECT_EQ(constraints.clock_groups.size(), 2u);
    EXPECT_TRUE(constraints.clock_groups[0].asynchronous);
    EXPECT_TRUE(constraints.clock_groups[1].asynchronous);
    EXPECT_EQ(constraints.clock_groups[0].clocks.size(), 1u);
    EXPECT_EQ(constraints.clock_groups[1].clocks.size(), 1u);
}

TEST_F(SdcReaderTest, ParseMultiCyclePath) {
    std::string sdc = R"(
set_multicycle_path 2 -from [get_clocks clk_slow] -to [get_clocks clk_fast]
)";
    
    auto constraints = reader.parse_sdc_content(sdc);
    
    EXPECT_EQ(constraints.multi_cycle_paths.size(), 1u);
    EXPECT_EQ(constraints.multi_cycle_paths[0].cycles, 2);
    EXPECT_EQ(constraints.multi_cycle_paths[0].from_clock, "clk_slow");
    EXPECT_EQ(constraints.multi_cycle_paths[0].to_clock, "clk_fast");
}

TEST_F(SdcReaderTest, CalculateFrequency) {
    std::string sdc = R"(
create_clock -name clk_100mhz -period 10.0
)";
    
    auto constraints = reader.parse_sdc_content(sdc);
    
    EXPECT_EQ(constraints.clocks.size(), 1u);
    EXPECT_NEAR(constraints.clocks[0].frequency_mhz, 100.0, 0.1);
}

TEST_F(SdcReaderTest, CommentsIgnored) {
    std::string sdc = R"(
# This is a comment
create_clock -name clk_core -period 10.0
# Another comment
)";
    
    auto constraints = reader.parse_sdc_content(sdc);
    
    EXPECT_EQ(constraints.clocks.size(), 1u);
}

TEST_F(ConstraintsTest, IsFalsePathEmptyClocksRequiresReg) {
    ClockConstraints c;
    // false_path with both clocks empty but reg fields set
    FalsePath fp;
    fp.from_reg = "mod.src";
    fp.to_reg = "mod.dst";
    c.false_paths.push_back(fp);

    // Match
    EXPECT_TRUE(c.is_false_path("top.mod.src", "top.mod.dst"));
    // No match: different register names
    EXPECT_FALSE(c.is_false_path("top.mod.other", "top.mod.dst"));
}

TEST_F(ConstraintsTest, IsFalsePathBothEmptyMatchesNothing) {
    ClockConstraints c;
    // false_path with everything empty — should match nothing
    FalsePath fp;
    c.false_paths.push_back(fp);

    EXPECT_FALSE(c.is_false_path("anything", "anything"));
}

TEST_F(ConstraintsTest, IsFalsePathClockBased) {
    ClockConstraints c;
    FalsePath fp;
    fp.from_clock = "clk_a";
    fp.to_clock = "clk_b";
    c.false_paths.push_back(fp);

    // Register names contain the clock name as substring
    EXPECT_TRUE(c.is_false_path("mod.clk_a_src_ff", "mod.clk_b_dst_ff"));
    // No match
    EXPECT_FALSE(c.is_false_path("mod.clk_c_src_ff", "mod.clk_b_dst_ff"));
}

TEST_F(ConstraintsTest, IsFalsePathClockWithRegRefines) {
    ClockConstraints c;
    FalsePath fp;
    fp.from_clock = "clk_a";
    fp.to_clock = "clk_b";
    fp.from_reg = "clk_a.special";
    c.false_paths.push_back(fp);

    // Clock matches and reg matches ("clk_a.special" is substring)
    EXPECT_TRUE(c.is_false_path("top.mod.clk_a.special.ff", "top.mod.clk_b.dst.ff"));
    // Clock matches but reg doesn't match ("clk_a.normal" is not "clk_a.special")
    EXPECT_FALSE(c.is_false_path("top.mod.clk_a.normal.ff", "top.mod.clk_b.dst.ff"));
}

TEST_F(SdcReaderTest, LineContinuation) {
    std::string sdc = R"(
set_false_path \
  -from [get_clocks clk_a] \
  -to [get_clocks clk_b]
)";

    auto constraints = reader.parse_sdc_content(sdc);

    ASSERT_EQ(constraints.false_paths.size(), 1u);
    EXPECT_EQ(constraints.false_paths[0].from_clock, "clk_a");
    EXPECT_EQ(constraints.false_paths[0].to_clock, "clk_b");
}

TEST_F(SdcReaderTest, WildcardFalsePath) {
    std::string sdc = R"(
set_false_path -from [get_clocks clk_*] -to [get_clocks jtag_*]
)";

    auto constraints = reader.parse_sdc_content(sdc);
    ASSERT_EQ(constraints.false_paths.size(), 1u);

    EXPECT_TRUE(constraints.is_false_path("mod.clk_core_ff", "mod.jtag_tck_ff"));
    EXPECT_TRUE(constraints.is_false_path("mod.clk_periph_ff", "mod.jtag_tms_ff"));
    EXPECT_FALSE(constraints.is_false_path("mod.clk_core_ff", "mod.clk_test_ff"));
}

TEST_F(SdcReaderTest, WildcardClockGroups) {
    std::string sdc = R"(
set_clock_groups -asynchronous -group [get_clocks clk_*] -group [get_clocks jtag_*]
)";

    auto constraints = reader.parse_sdc_content(sdc);
    ASSERT_EQ(constraints.clock_groups.size(), 2u);
    EXPECT_TRUE(constraints.is_asynchronous("clk_core", "jtag_tck"));
    EXPECT_TRUE(constraints.is_asynchronous("clk_periph", "jtag_tms"));
    EXPECT_FALSE(constraints.is_asynchronous("clk_core", "clk_periph"));
}

TEST_F(ConstraintsTest, WildcardFalsePathMatches) {
    ClockConstraints c;
    FalsePath fp;
    fp.from_clock = "clk_*";
    fp.to_clock = "async_*";
    c.false_paths.push_back(fp);

    EXPECT_TRUE(c.is_false_path("mod.clk_core_ff", "mod.async_dst_ff"));
    EXPECT_TRUE(c.is_false_path("mod.clk_slow_ff", "mod.async_meta_ff"));
    EXPECT_FALSE(c.is_false_path("mod.clk_core_ff", "mod.sync_dst_ff"));
}

TEST_F(ConstraintsTest, QuestionMarkWildcard) {
    ClockConstraints c;
    FalsePath fp;
    fp.from_clock = "clk?";
    c.false_paths.push_back(fp);

    EXPECT_TRUE(c.is_false_path("mod.clk0_ff", "mod.any_ff"));
    EXPECT_TRUE(c.is_false_path("mod.clk1_ff", "mod.any_ff"));
    EXPECT_FALSE(c.is_false_path("mod.clk10_ff", "mod.any_ff"));
}

TEST_F(ConstraintsTest, PatternMatchesExact) {
    EXPECT_TRUE(pattern_matches("clk_core", "clk_core"));
    EXPECT_FALSE(pattern_matches("clk_core", "clk_other"));
}

TEST_F(ConstraintsTest, PatternMatchesWildcard) {
    EXPECT_TRUE(pattern_matches("clk_*", "clk_core"));
    EXPECT_TRUE(pattern_matches("clk_*", "clk_periph"));
    EXPECT_FALSE(pattern_matches("clk_*", "jtag_tck"));
    EXPECT_TRUE(pattern_matches("clk?", "clk0"));
    EXPECT_FALSE(pattern_matches("clk?", "clk10"));
}

TEST_F(ConstraintsTest, PatternMatchesSegment) {
    EXPECT_TRUE(pattern_matches("clk_*", "top.clk_core_ff"));
    EXPECT_TRUE(pattern_matches("jtag_*", "mod.jtag_tck_ff"));
}

TEST_F(ConstraintsTest, PatternMatchesSubstring) {
    EXPECT_TRUE(pattern_matches("clk", "mod.clk_core_ff"));
    EXPECT_FALSE(pattern_matches("jtag", "mod.clk_core_ff"));
}

TEST_F(ConstraintsTest, PathMatchContextClockOnly) {
    ClockConstraints c;
    FalsePath fp;
    fp.from_clock = "clk_a";
    fp.to_clock = "clk_b";
    c.false_paths.push_back(fp);

    PathMatchContext ctx;
    ctx.source_clock = "clk_a";
    ctx.destination_clock = "clk_b";
    ctx.source_register = "mod.src_ff";
    ctx.destination_register = "mod.dst_ff";
    EXPECT_TRUE(c.is_false_path(ctx));

    ctx.destination_clock = "clk_c";
    EXPECT_FALSE(c.is_false_path(ctx));
}

TEST_F(ConstraintsTest, PathMatchContextClockAndReg) {
    ClockConstraints c;
    FalsePath fp;
    fp.from_clock = "clk_a";
    fp.to_clock = "clk_b";
    fp.from_reg = "special_src";
    c.false_paths.push_back(fp);

    PathMatchContext ctx;
    ctx.source_clock = "clk_a";
    ctx.destination_clock = "clk_b";
    ctx.source_register = "top.special_src_ff";
    ctx.destination_register = "mod.dst_ff";
    EXPECT_TRUE(c.is_false_path(ctx));

    ctx.source_register = "top.normal_src_ff";
    EXPECT_FALSE(c.is_false_path(ctx));
}

TEST_F(ConstraintsTest, PathMatchContextCell) {
    ClockConstraints c;
    FalsePath fp;
    fp.from_cell = "top.u_src";
    fp.to_cell = "top.u_dst";
    c.false_paths.push_back(fp);

    PathMatchContext ctx;
    ctx.source_cell = "top.u_src";
    ctx.destination_cell = "top.u_dst";
    EXPECT_TRUE(c.is_false_path(ctx));

    ctx.destination_cell = "top.u_other";
    EXPECT_FALSE(c.is_false_path(ctx));
}

TEST_F(ConstraintsTest, PathMatchContextCellWildcard) {
    ClockConstraints c;
    FalsePath fp;
    fp.from_cell = "top.u_src*";
    fp.to_cell = "top.u_dst*";
    c.false_paths.push_back(fp);

    PathMatchContext ctx;
    ctx.source_cell = "top.u_src_0";
    ctx.destination_cell = "top.u_dst_0";
    EXPECT_TRUE(c.is_false_path(ctx));

    ctx.source_cell = "top.u_other";
    EXPECT_FALSE(c.is_false_path(ctx));
}

TEST_F(ConstraintsTest, PathMatchContextPin) {
    ClockConstraints c;
    FalsePath fp;
    fp.from_pin = "top.u_src/Q";
    fp.to_pin = "top.u_dst/D";
    c.false_paths.push_back(fp);

    PathMatchContext ctx;
    ctx.source_pin = "top.u_src/Q";
    ctx.destination_pin = "top.u_dst/D";
    EXPECT_TRUE(c.is_false_path(ctx));

    ctx.destination_pin = "top.u_dst/CK";
    EXPECT_FALSE(c.is_false_path(ctx));
}

TEST_F(ConstraintsTest, PathMatchContextThrough) {
    ClockConstraints c;
    FalsePath fp;
    fp.from_clock = "clk_a";
    fp.to_clock = "clk_b";
    fp.through = {"top.u_mux"};
    c.false_paths.push_back(fp);

    PathMatchContext ctx;
    ctx.source_clock = "clk_a";
    ctx.destination_clock = "clk_b";
    ctx.path_nodes = {"top.reg_a", "top.u_mux", "top.reg_b"};
    EXPECT_TRUE(c.is_false_path(ctx));

    ctx.path_nodes = {"top.reg_a", "top.reg_b"};
    EXPECT_FALSE(c.is_false_path(ctx));
}

TEST_F(ConstraintsTest, PathMatchContextThroughWildcard) {
    ClockConstraints c;
    FalsePath fp;
    fp.through = {"top.u_mux*"};
    c.false_paths.push_back(fp);

    PathMatchContext ctx;
    ctx.path_nodes = {"top.reg_a", "top.u_mux_0", "top.reg_b"};
    EXPECT_TRUE(c.is_false_path(ctx));

    ctx.path_nodes = {"top.reg_a", "top.reg_b"};
    EXPECT_FALSE(c.is_false_path(ctx));
}

TEST_F(ConstraintsTest, PathMatchContextMultipleThrough) {
    ClockConstraints c;
    FalsePath fp;
    fp.from_clock = "clk_a";
    fp.to_clock = "clk_b";
    fp.through = {"top.u_mux", "top.u_buf"};
    c.false_paths.push_back(fp);

    PathMatchContext ctx;
    ctx.source_clock = "clk_a";
    ctx.destination_clock = "clk_b";
    ctx.path_nodes = {"top.reg_a", "top.u_mux", "top.u_buf", "top.reg_b"};
    EXPECT_TRUE(c.is_false_path(ctx));

    ctx.path_nodes = {"top.reg_a", "top.u_mux", "top.reg_b"};
    EXPECT_FALSE(c.is_false_path(ctx));
}

TEST_F(ConstraintsTest, PathMatchContextEmptyMatchesNothing) {
    ClockConstraints c;
    FalsePath fp;
    c.false_paths.push_back(fp);

    PathMatchContext ctx;
    ctx.source_clock = "anything";
    ctx.destination_clock = "anything";
    EXPECT_FALSE(c.is_false_path(ctx));
}

TEST_F(SdcReaderTest, ParseFalsePathGetCells) {
    std::string sdc = "set_false_path -from [get_cells top.u_src] -to [get_cells top.u_dst]\n";

    auto constraints = reader.parse_sdc_content(sdc);
    ASSERT_EQ(constraints.false_paths.size(), 1u);
    EXPECT_EQ(constraints.false_paths[0].from_cell, "top.u_src");
    EXPECT_EQ(constraints.false_paths[0].to_cell, "top.u_dst");
}

TEST_F(SdcReaderTest, ParseFalsePathGetPins) {
    std::string sdc = "set_false_path -from [get_pins top.u_src/Q] -to [get_pins top.u_dst/D]\n";

    auto constraints = reader.parse_sdc_content(sdc);
    ASSERT_EQ(constraints.false_paths.size(), 1u);
    EXPECT_EQ(constraints.false_paths[0].from_pin, "top.u_src/Q");
    EXPECT_EQ(constraints.false_paths[0].to_pin, "top.u_dst/D");
}

TEST_F(SdcReaderTest, ParseFalsePathThroughPin) {
    std::string sdc = "set_false_path -from [get_clocks clk_a] -through [get_pins u_mux/Y] -to [get_clocks clk_b]\n";

    auto constraints = reader.parse_sdc_content(sdc);
    ASSERT_EQ(constraints.false_paths.size(), 1u);
    EXPECT_EQ(constraints.false_paths[0].from_clock, "clk_a");
    EXPECT_EQ(constraints.false_paths[0].to_clock, "clk_b");
    ASSERT_EQ(constraints.false_paths[0].through.size(), 1u);
    EXPECT_EQ(constraints.false_paths[0].through[0], "u_mux/Y");
}

TEST_F(SdcReaderTest, ParseFalsePathMultipleThrough) {
    std::string sdc = "set_false_path -through u_mux -through u_buf\n";

    auto constraints = reader.parse_sdc_content(sdc);
    ASSERT_EQ(constraints.false_paths.size(), 1u);
    ASSERT_EQ(constraints.false_paths[0].through.size(), 2u);
    EXPECT_EQ(constraints.false_paths[0].through[0], "u_mux");
    EXPECT_EQ(constraints.false_paths[0].through[1], "u_buf");
}