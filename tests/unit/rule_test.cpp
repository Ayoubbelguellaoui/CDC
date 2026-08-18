#include "rules/rule.h"
#include "cdc/crossing.h"
#include <gtest/gtest.h>

using namespace opencdc::rules;

class RuleEngineTest : public ::testing::Test {
protected:
    RuleEngine engine;
};

TEST_F(RuleEngineTest, DefaultRulesLoaded) {
    auto rules = engine.rules();
    ASSERT_EQ(rules.size(), 3u);
    EXPECT_EQ(rules[0].id, "CDC001");
    EXPECT_EQ(rules[1].id, "CDC002");
    EXPECT_EQ(rules[2].id, "CDC003");
}

TEST_F(RuleEngineTest, FindRule) {
    auto r = engine.find_rule("CDC001");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->name, "unsynchronized_crossing");
    EXPECT_EQ(r->severity, "error");
    EXPECT_TRUE(r->enabled);
}

TEST_F(RuleEngineTest, FindNonexistentReturnsNullopt) {
    EXPECT_FALSE(engine.find_rule("CDC999").has_value());
}

TEST_F(RuleEngineTest, DisableRuleFiltersFindings) {
    engine.add_override({"CDC001", "", true, false});

    Finding f;
    f.rule_id = "CDC001";
    f.rule_name = "unsynchronized_crossing";
    f.severity = "error";

    std::vector<Finding> findings = {f};
    auto filtered = engine.filter(findings);
    EXPECT_TRUE(filtered.empty());
}

TEST_F(RuleEngineTest, EnableDisabledRuleRestoresFindings) {
    engine.add_override({"CDC001", "", true, false});
    engine.add_override({"CDC001", "", true, true});

    Finding f;
    f.rule_id = "CDC001";
    f.rule_name = "unsynchronized_crossing";
    f.severity = "error";

    std::vector<Finding> findings = {f};
    auto filtered = engine.filter(findings);
    EXPECT_EQ(filtered.size(), 1u);
}

TEST_F(RuleEngineTest, SeverityOverrideApplied) {
    engine.add_override({"CDC001", "warning", false, true});

    Finding f;
    f.rule_id = "CDC001";
    f.rule_name = "unsynchronized_crossing";
    f.severity = "error";

    std::vector<Finding> findings = {f};
    auto filtered = engine.filter(findings);
    ASSERT_EQ(filtered.size(), 1u);
    EXPECT_EQ(filtered[0].severity, "warning");
}

TEST_F(RuleEngineTest, UnknownRuleFindingPassesThrough) {
    Finding f;
    f.rule_id = "CDC999";
    f.rule_name = "unknown";
    f.severity = "error";

    std::vector<Finding> findings = {f};
    auto filtered = engine.filter(findings);
    ASSERT_EQ(filtered.size(), 1u);
    EXPECT_EQ(filtered[0].severity, "error");
}

TEST_F(RuleEngineTest, MultipleFindingsMixedRules) {
    Finding f1;
    f1.rule_id = "CDC001";
    f1.rule_name = "unsynchronized_crossing";
    f1.severity = "error";

    Finding f2;
    f2.rule_id = "CDC003";
    f2.rule_name = "reconvergence_hazard";
    f2.severity = "warning";

    std::vector<Finding> findings = {f1, f2};
    auto filtered = engine.filter(findings);
    EXPECT_EQ(filtered.size(), 2u);
}

TEST_F(RuleEngineTest, IsEnabledDefault) {
    EXPECT_TRUE(engine.is_enabled("CDC001"));
    EXPECT_TRUE(engine.is_enabled("CDC003"));
    EXPECT_TRUE(engine.is_enabled("CDC999"));
}

TEST_F(RuleEngineTest, IsEnabledAfterDisable) {
    engine.add_override({"CDC001", "", true, false});
    EXPECT_FALSE(engine.is_enabled("CDC001"));
    EXPECT_TRUE(engine.is_enabled("CDC003"));
}
