#include "cdc/waiver.h"
#include <gtest/gtest.h>
#include <fstream>

using namespace opencdc::cdc;

class WaiverTest : public ::testing::Test {
protected:
    WaiverEngine engine;

    Finding make_finding(const std::string& rule, const std::string& src,
                         const std::string& dst, const std::string& src_dom,
                         const std::string& dst_dom) {
        Finding f;
        f.rule_id = rule;
        f.source_reg_name = src;
        f.dest_reg_name = dst;
        f.source_domain = src_dom;
        f.dest_domain = dst_dom;
        f.severity = "error";
        return f;
    }
};

TEST_F(WaiverTest, ExactMatch) {
    Waiver w;
    w.rule_id = "CDC001";
    w.source_reg_name = "mod.src";
    w.dest_reg_name = "mod.dst";
    w.source_domain = "clk_a";
    w.dest_domain = "clk_b";
    w.justification = "Reviewed safe";
    w.owner = "john";
    engine.add_waiver(w);

    auto f = make_finding("CDC001", "mod.src", "mod.dst", "clk_a", "clk_b");
    std::vector<Finding> findings = {f};
    auto result = engine.apply(findings);

    ASSERT_EQ(result.size(), 1u);
    EXPECT_TRUE(result[0].waived);
    EXPECT_EQ(result[0].waiver_justification, "Reviewed safe");
    EXPECT_EQ(result[0].waiver_owner, "john");
}

TEST_F(WaiverTest, NoMatchDifferentRule) {
    Waiver w;
    w.rule_id = "CDC002";
    w.source_reg_name = "mod.src";
    w.dest_reg_name = "mod.dst";
    w.source_domain = "clk_a";
    w.dest_domain = "clk_b";
    engine.add_waiver(w);

    auto f = make_finding("CDC001", "mod.src", "mod.dst", "clk_a", "clk_b");
    std::vector<Finding> findings = {f};
    auto result = engine.apply(findings);

    ASSERT_EQ(result.size(), 1u);
    EXPECT_FALSE(result[0].waived);
}

TEST_F(WaiverTest, NoMatchDifferentDomains) {
    Waiver w;
    w.rule_id = "CDC001";
    w.source_reg_name = "mod.src";
    w.dest_reg_name = "mod.dst";
    w.source_domain = "clk_a";
    w.dest_domain = "clk_c";
    engine.add_waiver(w);

    auto f = make_finding("CDC001", "mod.src", "mod.dst", "clk_a", "clk_b");
    std::vector<Finding> findings = {f};
    auto result = engine.apply(findings);

    ASSERT_EQ(result.size(), 1u);
    EXPECT_FALSE(result[0].waived);
}

TEST_F(WaiverTest, ExpiredWaiverSkipped) {
    Waiver w;
    w.rule_id = "CDC001";
    w.source_reg_name = "mod.src";
    w.dest_reg_name = "mod.dst";
    w.source_domain = "clk_a";
    w.dest_domain = "clk_b";
    w.expiry = "2020-01-01";
    engine.add_waiver(w);

    auto f = make_finding("CDC001", "mod.src", "mod.dst", "clk_a", "clk_b");
    std::vector<Finding> findings = {f};
    auto result = engine.apply(findings);

    ASSERT_EQ(result.size(), 1u);
    EXPECT_FALSE(result[0].waived);
}

TEST_F(WaiverTest, CaseInsensitiveMatch) {
    Waiver w;
    w.rule_id = "CDC001";
    w.source_reg_name = "MOD.SRC";
    w.dest_reg_name = "MOD.DST";
    w.source_domain = "CLK_A";
    w.dest_domain = "CLK_B";
    engine.add_waiver(w);

    auto f = make_finding("CDC001", "mod.src", "mod.dst", "clk_a", "clk_b");
    std::vector<Finding> findings = {f};
    auto result = engine.apply(findings);

    ASSERT_EQ(result.size(), 1u);
    EXPECT_TRUE(result[0].waived);
}

TEST_F(WaiverTest, MultipleWaivers) {
    Waiver w1;
    w1.rule_id = "CDC001";
    w1.source_reg_name = "mod.src";
    w1.dest_reg_name = "mod.dst";
    w1.source_domain = "clk_a";
    w1.dest_domain = "clk_b";
    w1.justification = "First waiver";
    engine.add_waiver(w1);

    Waiver w2;
    w2.rule_id = "CDC001";
    w2.source_reg_name = "mod.src2";
    w2.dest_reg_name = "mod.dst2";
    w2.source_domain = "clk_a";
    w2.dest_domain = "clk_b";
    w2.justification = "Second waiver";
    engine.add_waiver(w2);

    auto f1 = make_finding("CDC001", "mod.src", "mod.dst", "clk_a", "clk_b");
    auto f2 = make_finding("CDC001", "mod.src2", "mod.dst2", "clk_a", "clk_b");
    auto f3 = make_finding("CDC001", "unrelated.src", "unrelated.dst", "clk_a", "clk_b");
    std::vector<Finding> findings = {f1, f2, f3};
    auto result = engine.apply(findings);

    ASSERT_EQ(result.size(), 3u);
    EXPECT_TRUE(result[0].waived);
    EXPECT_TRUE(result[1].waived);
    EXPECT_FALSE(result[2].waived);
}

TEST_F(WaiverTest, LoadFromFile) {
    std::string path = "/tmp/test_waivers.txt";
    {
        std::ofstream f(path);
        f << "# comment line\n";
        f << "CDC001 mod.src mod.dst clk_a clk_b \"Reviewed safe\" @john 2099-12-31\n";
        f << "CDC002 mod.a mod.b clk_x clk_y \"Unchecked\" @jane\n";
    }

    WaiverEngine file_engine;
    ASSERT_TRUE(file_engine.load_from_file(path));
    ASSERT_EQ(file_engine.waivers().size(), 2u);

    auto f = make_finding("CDC001", "mod.src", "mod.dst", "clk_a", "clk_b");
    std::vector<Finding> findings = {f};
    auto result = file_engine.apply(findings);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_TRUE(result[0].waived);

    std::remove(path.c_str());
}

TEST_F(WaiverTest, MalformedDateDoesNotCrash) {
    WaiverEngine test_engine;
    Waiver w;
    w.rule_id = "CDC001";
    w.source_reg_name = "mod.src";
    w.dest_reg_name = "mod.dst";
    w.expiry = "202A-13-99";
    test_engine.add_waiver(w);

    auto f = make_finding("CDC001", "mod.src", "mod.dst", "clk_a", "clk_b");
    std::vector<Finding> findings = {f};
    auto result = test_engine.apply(findings);

    ASSERT_EQ(result.size(), 1u);
    EXPECT_FALSE(result[0].waived);
}

TEST_F(WaiverTest, EmptyRuleIdWaiverDoesNotMatchEverything) {
    // A waiver with an empty rule id must not act as a universal waiver.
    Waiver w;
    w.source_reg_name = "mod.src";
    w.dest_reg_name = "mod.dst";
    engine.add_waiver(w);

    auto f = make_finding("CDC001", "mod.src", "mod.dst", "clk_a", "clk_b");
    std::vector<Finding> findings = {f};
    auto result = engine.apply(findings);

    ASSERT_EQ(result.size(), 1u);
    EXPECT_FALSE(result[0].waived);
}

TEST_F(WaiverTest, EmptyWaiverFieldStillActsAsWildcard) {
    // Deliberate wildcard use: empty domain fields match any domain, but the
    // rule id and register names are specific.
    Waiver w;
    w.rule_id = "CDC001";
    w.source_reg_name = "mod.src";
    w.dest_reg_name = "mod.dst";
    engine.add_waiver(w);

    auto f1 = make_finding("CDC001", "mod.src", "mod.dst", "clk_a", "clk_b");
    auto f2 = make_finding("CDC001", "mod.src", "other.dst", "clk_a", "clk_b");
    std::vector<Finding> findings = {f1, f2};
    auto result = engine.apply(findings);

    ASSERT_EQ(result.size(), 2u);
    EXPECT_TRUE(result[0].waived);
    EXPECT_FALSE(result[1].waived);
}

TEST_F(WaiverTest, SpecificWaiverDoesNotMatchEmptyFindingField) {
    // CDC010-style findings have an empty dest register; a waiver naming a
    // specific dest must not waive them.
    Waiver w;
    w.rule_id = "CDC010";
    w.source_reg_name = "mod.src";
    w.dest_reg_name = "mod.dst";
    engine.add_waiver(w);

    Finding f;
    f.rule_id = "CDC010";
    f.source_reg_name = "mod.src";
    f.dest_reg_name = "";  // truncation findings carry no dest
    std::vector<Finding> findings = {f};
    auto result = engine.apply(findings);

    ASSERT_EQ(result.size(), 1u);
    EXPECT_FALSE(result[0].waived);
}

TEST_F(WaiverTest, LoadFromFileRejectsMissingRuleId) {
    // A WILDCARD/REGEX line missing its rule id field would otherwise match
    // every finding of every rule.
    std::string path = "/tmp/test_waivers_norule.txt";
    {
        std::ofstream f(path);
        f << "WILDCARD top.*.src top.*.dst\n";
    }

    WaiverEngine file_engine;
    std::string error;
    EXPECT_FALSE(file_engine.load_from_file(path, &error));
    EXPECT_TRUE(file_engine.waivers().empty());
    EXPECT_FALSE(error.empty());

    std::remove(path.c_str());
}

TEST_F(WaiverTest, AddWaiverRejectsEmptyRuleId) {
    Waiver w;
    w.source_reg_name = "mod.src";
    w.dest_reg_name = "mod.dst";
    EXPECT_FALSE(engine.add_waiver(w));
    EXPECT_TRUE(engine.waivers().empty());
}

TEST_F(WaiverTest, LoadFromFileEmptyFileFails) {
    std::string path = "/tmp/test_waivers_empty.txt";
    {
        std::ofstream f(path);
        f << "# only comments\n\n";
    }

    WaiverEngine file_engine;
    std::string error;
    EXPECT_FALSE(file_engine.load_from_file(path, &error));
    EXPECT_FALSE(error.empty());

    std::remove(path.c_str());
}

TEST_F(WaiverTest, ExpiryDateIsValidThroughEndOfDay) {
    Waiver w;
    w.rule_id = "CDC001";
    w.source_reg_name = "mod.src";
    w.dest_reg_name = "mod.dst";
    // Far-future date: inclusive end-of-day semantics keep it valid.
    w.expiry = "2099-12-31";
    engine.add_waiver(w);

    auto f = make_finding("CDC001", "mod.src", "mod.dst", "clk_a", "clk_b");
    std::vector<Finding> findings = {f};
    auto result = engine.apply(findings);

    ASSERT_EQ(result.size(), 1u);
    EXPECT_TRUE(result[0].waived);
}
