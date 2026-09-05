#include "cdc/waiver.h"
#include "cdc/crossing.h"
#include <gtest/gtest.h>
#include <fstream>

using namespace opencdc::cdc;

class WaiverRegexTest : public ::testing::Test {
protected:
    WaiverEngine engine;
};

TEST_F(WaiverRegexTest, SubstringMatch) {
    Waiver w;
    w.rule_id = "CDC001";
    w.source_reg_name = "module.src_reg";
    w.dest_reg_name = "module.dst_reg";
    w.match_type = WaiverMatchType::Substring;
    
    engine.add_waiver(w);
    
    Finding f;
    f.rule_id = "CDC001";
    f.source_reg_name = "top.module.src_reg";
    f.dest_reg_name = "top.module.dst_reg";
    
    EXPECT_TRUE(engine.matches(f, engine.waivers()[0]));
}

TEST_F(WaiverRegexTest, SubstringMatchNoOvermatch) {
    Waiver w;
    w.rule_id = "CDC001";
    w.source_reg_name = "mod.src";
    w.dest_reg_name = "mod.dst";
    w.match_type = WaiverMatchType::Substring;
    
    engine.add_waiver(w);
    
    Finding f;
    f.rule_id = "CDC001";
    f.source_reg_name = "other_mod.src_extra";
    f.dest_reg_name = "other_mod.dst_extra";
    
    EXPECT_FALSE(engine.matches(f, engine.waivers()[0]))
        << "Substring match should not overmatch across hierarchical boundaries";
}

TEST_F(WaiverRegexTest, WildcardMatchSingle) {
    Waiver w;
    w.rule_id = "CDC001";
    w.source_reg_name = "top.*.src";
    w.dest_reg_name = "top.*.dst";
    w.match_type = WaiverMatchType::Wildcard;
    
    engine.add_waiver(w);
    
    Finding f;
    f.rule_id = "CDC001";
    f.source_reg_name = "top.module1.src";
    f.dest_reg_name = "top.module2.dst";
    
    EXPECT_TRUE(engine.matches(f, engine.waivers()[0]));
}

TEST_F(WaiverRegexTest, WildcardMatchMultiple) {
    Waiver w;
    w.rule_id = "CDC001";
    w.source_reg_name = "top.*.*.src_*";
    w.dest_reg_name = "top.*.*.dst_*";
    w.match_type = WaiverMatchType::Wildcard;
    
    engine.add_waiver(w);
    
    Finding f;
    f.rule_id = "CDC001";
    f.source_reg_name = "top.a.b.src_reg";
    f.dest_reg_name = "top.c.d.dst_reg";
    
    EXPECT_TRUE(engine.matches(f, engine.waivers()[0]));
}

TEST_F(WaiverRegexTest, WildcardQuestionMark) {
    Waiver w;
    w.rule_id = "CDC001";
    w.source_reg_name = "src_?";
    w.dest_reg_name = "dst_?";
    w.match_type = WaiverMatchType::Wildcard;
    
    engine.add_waiver(w);
    
    Finding f;
    f.rule_id = "CDC001";
    f.source_reg_name = "src_a";
    f.dest_reg_name = "dst_b";
    
    EXPECT_TRUE(engine.matches(f, engine.waivers()[0]));
}

TEST_F(WaiverRegexTest, RegexMatch) {
    Waiver w;
    w.rule_id = "CDC001";
    w.source_reg_name = "top\\.\\w+\\.src_\\d+";
    w.dest_reg_name = "top\\.\\w+\\.dst_\\d+";
    w.match_type = WaiverMatchType::Regex;
    
    engine.add_waiver(w);
    
    Finding f;
    f.rule_id = "CDC001";
    f.source_reg_name = "top.module.src_123";
    f.dest_reg_name = "top.module.dst_456";
    
    EXPECT_TRUE(engine.matches(f, engine.waivers()[0]));
}

TEST_F(WaiverRegexTest, RegexNoMatch) {
    Waiver w;
    w.rule_id = "CDC001";
    w.source_reg_name = "top\\.\\w+\\.src";
    w.dest_reg_name = "top\\.\\w+\\.dst";
    w.match_type = WaiverMatchType::Regex;
    
    engine.add_waiver(w);
    
    Finding f;
    f.rule_id = "CDC001";
    f.source_reg_name = "top.module.other";
    f.dest_reg_name = "top.module.dst";
    
    EXPECT_FALSE(engine.matches(f, engine.waivers()[0]));
}

TEST_F(WaiverRegexTest, LoadFromFileWithWildcard) {
    std::string waiver_content = 
        "WILDCARD CDC001 top.*.src top.*.dst \"Test waiver\" @team\n";
    
    std::string temp_file = "/tmp/test_waivers.txt";
    std::ofstream file(temp_file);
    file << waiver_content;
    file.close();
    
    EXPECT_TRUE(engine.load_from_file(temp_file));
    EXPECT_EQ(engine.waivers().size(), 1u);
    EXPECT_EQ(engine.waivers()[0].match_type, WaiverMatchType::Wildcard);
    
    std::remove(temp_file.c_str());
}

TEST_F(WaiverRegexTest, LoadFromFileWithRegex) {
    std::string waiver_content = 
        "REGEX CDC001 top\\.\\w+\\.src top\\.\\w+\\.dst \"Test waiver\" @team\n";
    
    std::string temp_file = "/tmp/test_waivers_regex.txt";
    std::ofstream file(temp_file);
    file << waiver_content;
    file.close();
    
    EXPECT_TRUE(engine.load_from_file(temp_file));
    EXPECT_EQ(engine.waivers().size(), 1u);
    EXPECT_EQ(engine.waivers()[0].match_type, WaiverMatchType::Regex);
    
    std::remove(temp_file.c_str());
}

TEST_F(WaiverRegexTest, ApplyWildcardWaivers) {
    Waiver w;
    w.rule_id = "CDC001";
    w.source_reg_name = "top.*.src_*";
    w.dest_reg_name = "top.*.dst_*";
    w.match_type = WaiverMatchType::Wildcard;
    w.justification = "Module-level waiver";
    w.owner = "@team";
    
    engine.add_waiver(w);
    
    std::vector<Finding> findings;
    Finding f1;
    f1.rule_id = "CDC001";
    f1.source_reg_name = "top.mod1.src_reg";
    f1.dest_reg_name = "top.mod1.dst_reg";
    findings.push_back(f1);
    
    Finding f2;
    f2.rule_id = "CDC001";
    f2.source_reg_name = "top.mod2.other";
    f2.dest_reg_name = "top.mod2.dst";
    findings.push_back(f2);
    
    auto result = engine.apply(findings);
    
    EXPECT_TRUE(result[0].waived);
    EXPECT_FALSE(result[1].waived);
}

TEST_F(WaiverRegexTest, CaseInsensitiveMatching) {
    Waiver w;
    w.rule_id = "CDC001";
    w.source_reg_name = "TOP.*.SRC";
    w.dest_reg_name = "top.*.dst";
    w.match_type = WaiverMatchType::Wildcard;
    
    engine.add_waiver(w);
    
    Finding f;
    f.rule_id = "cdc001";
    f.source_reg_name = "top.module.src";
    f.dest_reg_name = "TOP.MODULE.DST";
    
    EXPECT_TRUE(engine.matches(f, engine.waivers()[0]));
}

TEST_F(WaiverRegexTest, InvalidRegexIsNotUsedAsSubstring) {
    Waiver w;
    w.rule_id = "CDC001";
    w.source_reg_name = "[";
    w.match_type = WaiverMatchType::Regex;
    engine.add_waiver(w);

    EXPECT_TRUE(engine.waivers().empty());
}
