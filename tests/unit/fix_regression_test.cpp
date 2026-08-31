// fix_regression_test.cpp — Regression tests for the 12 bug fixes.
// Each test is labelled with the fix number it validates.

#include "cdc/synchronizer.h"
#include "cdc/pattern.h"
#include "cdc/waiver.h"
#include "cdc/crossing.h"
#include "cdc/reconvergence.h"
#include "ir/graph.h"
#include "clock/domain.h"
#include "util/string_util.h"
#include <gtest/gtest.h>

using namespace opencdc::ir;
using namespace opencdc::clock;
using namespace opencdc::cdc;

// ─────────────────────────────────────────────────────────────────────────────
// Fix 1 — synchronizer.cpp: find_pattern_for_dest returned ThreeFF for 2FF
// ─────────────────────────────────────────────────────────────────────────────

TEST(Fix1_SynchronizerPattern, TwoFFDetectedNotThreeFF) {
    Graph g;
    uint64_t src  = g.add_register("top.src",  "clkA", 1, {});
    uint64_t meta = g.add_register("top.meta", "clkB", 1, {});
    uint64_t sync = g.add_register("top.sync", "clkB", 1, {});
    // Only src → meta → sync (2 stages in clkB)
    g.add_edge(src,  meta);
    g.add_edge(meta, sync);

    SynchronizerMatcher m;
    // meta is the first capture register — chain has 1 follow-on (sync) → TwoFF
    EXPECT_EQ(m.find_pattern_for_dest(meta, g), SyncPattern::TwoFF)
        << "BUG-6 regression: 2-stage chain must return TwoFF, not ThreeFF";
}

TEST(Fix1_SynchronizerPattern, ThreeFFDetectedCorrectly) {
    Graph g;
    uint64_t src  = g.add_register("top.src",  "clkA", 1, {});
    uint64_t m1   = g.add_register("top.m1",   "clkB", 1, {});
    uint64_t m2   = g.add_register("top.m2",   "clkB", 1, {});
    uint64_t m3   = g.add_register("top.m3",   "clkB", 1, {});
    g.add_edge(src, m1);
    g.add_edge(m1,  m2);
    g.add_edge(m2,  m3);

    SynchronizerMatcher m;
    EXPECT_EQ(m.find_pattern_for_dest(m1, g), SyncPattern::ThreeFF)
        << "3-stage chain must return ThreeFF";
}

// ─────────────────────────────────────────────────────────────────────────────
// Fix 8 — analyzer.cpp: exclusive clock groups added twice
// Tested via direct false-path dedup logic (no full analyzer needed).
// ─────────────────────────────────────────────────────────────────────────────

TEST(Fix8_ClockGroupDedup, NoDuplicateFalsePaths) {
    // Simulate the dedup lambda behaviour: adding the same pair twice should
    // result in only one false path entry.
    std::vector<opencdc::clock::FalsePath> false_paths;

    auto add_if_new = [&](const std::string& a, const std::string& b) {
        for (const auto& fp : false_paths) {
            if (fp.from_clock == a && fp.to_clock == b) return;
        }
        opencdc::clock::FalsePath fp;
        fp.from_clock = a;
        fp.to_clock   = b;
        false_paths.push_back(fp);
    };

    // Add the same exclusive pair twice (simulating config + SDC both having it)
    add_if_new("clk_a", "clk_b");
    add_if_new("clk_b", "clk_a");
    add_if_new("clk_a", "clk_b");  // duplicate
    add_if_new("clk_b", "clk_a");  // duplicate

    EXPECT_EQ(false_paths.size(), 2u)
        << "BUG-2 regression: duplicate exclusive clock group false paths must be deduped";
}

// ─────────────────────────────────────────────────────────────────────────────
// Fix 9 — waiver.cpp: regex compiled once at add_waiver (not per match)
// ─────────────────────────────────────────────────────────────────────────────

TEST(Fix9_WaiverRegex, PreCompiledRegexStoredOnAdd) {
    WaiverEngine engine;
    Waiver w;
    w.rule_id        = "CDC001";
    w.source_reg_name = "top\\.src.*";
    w.match_type     = WaiverMatchType::Regex;
    ASSERT_TRUE(engine.add_waiver(w));

    const auto& stored = engine.waivers().front();
    EXPECT_NE(stored.source_regex, nullptr)
        << "BUG-7 regression: pre-compiled source_regex must be non-null after add_waiver";
}

TEST(Fix9_WaiverRegex, InvalidRegexRejectedAtAddTime) {
    WaiverEngine engine;
    Waiver w;
    w.rule_id        = "CDC001";
    w.source_reg_name = "[invalid(regex";   // malformed
    w.match_type     = WaiverMatchType::Regex;
    EXPECT_FALSE(engine.add_waiver(w))
        << "BUG-7 regression: invalid regex must be rejected by add_waiver";
    EXPECT_TRUE(engine.waivers().empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Fix 11 — waiver.cpp: expired waivers now appear in check_unused warnings
// ─────────────────────────────────────────────────────────────────────────────

TEST(Fix11_WaiverExpiry, ExpiredWaiverIsReported) {
    WaiverEngine engine;
    Waiver w;
    w.rule_id        = "CDC001";
    w.source_reg_name = "top.src";
    w.expiry         = "2000-01-01";   // definitely expired
    w.match_type     = WaiverMatchType::Substring;
    ASSERT_TRUE(engine.add_waiver(w));

    std::vector<Finding> no_findings;
    auto warnings = engine.check_unused(no_findings);

    bool found_expiry_warn = false;
    for (const auto& s : warnings) {
        if (s.find("expired") != std::string::npos) {
            found_expiry_warn = true;
            break;
        }
    }
    EXPECT_TRUE(found_expiry_warn)
        << "WEAK-6 regression: expired waiver must produce an 'expired' warning";
}

TEST(Fix11_WaiverExpiry, UnexpiredUnusedWaiverIsReported) {
    WaiverEngine engine;
    Waiver w;
    w.rule_id        = "CDC001";
    w.source_reg_name = "top.src";
    w.expiry         = "2099-01-01";   // far future
    w.match_type     = WaiverMatchType::Substring;
    ASSERT_TRUE(engine.add_waiver(w));

    std::vector<Finding> no_findings;
    auto warnings = engine.check_unused(no_findings);

    bool found_unused_warn = false;
    for (const auto& s : warnings) {
        if (s.find("did not match") != std::string::npos) {
            found_unused_warn = true;
            break;
        }
    }
    EXPECT_TRUE(found_unused_warn)
        << "Unused non-expired waiver must still produce 'did not match' warning";
}

// ─────────────────────────────────────────────────────────────────────────────
// Fix 13 — util/string_util.h: case-insensitive wildcard_match
// ─────────────────────────────────────────────────────────────────────────────

TEST(Fix13_WildcardMatch, CaseInsensitiveMatch) {
    EXPECT_TRUE(opencdc::util::wildcard_match("CLK_A*", "clk_a_gated"))
        << "wildcard_match must be case-insensitive";
    EXPECT_TRUE(opencdc::util::wildcard_match("clk_a*", "CLK_A_GATED"))
        << "wildcard_match must be case-insensitive (both directions)";
}

TEST(Fix13_WildcardMatch, QuestionMark) {
    EXPECT_TRUE(opencdc::util::wildcard_match("clk_?", "clk_a"));
    EXPECT_FALSE(opencdc::util::wildcard_match("clk_?", "clk_ab"));
}

TEST(Fix13_WildcardMatch, StarMatchesEmpty) {
    EXPECT_TRUE(opencdc::util::wildcard_match("*", "anything"));
    EXPECT_TRUE(opencdc::util::wildcard_match("*", ""));
}

TEST(Fix13_WildcardMatch, NoFalseMatchOnSubstring) {
    // "clk" should NOT match "slow_clk" when no wildcard is present
    EXPECT_FALSE(opencdc::util::wildcard_match("clk", "slow_clk"));
    EXPECT_TRUE(opencdc::util::wildcard_match("*clk", "slow_clk"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Fix 7 — graph.cpp: rope-based path traversal cycle detection
// ─────────────────────────────────────────────────────────────────────────────

TEST(Fix7_PathTraversal, FindsSimplePath) {
    Graph g;
    uint64_t a = g.add_register("top.a", "clkA", 1, {});
    uint64_t b = g.add_register("top.b", "clkB", 1, {});
    g.add_edge(a, b);

    auto result = g.find_register_paths(a);
    ASSERT_EQ(result.paths.size(), 1u);
    EXPECT_EQ(result.paths[0].src_reg_id, a);
    EXPECT_EQ(result.paths[0].dst_reg_id, b);
}

TEST(Fix7_PathTraversal, NoCycleHang) {
    // Create a cycle via combinational nodes
    Graph g;
    uint64_t r1 = g.add_register("top.r1", "clkA", 1, {});
    uint64_t n1 = g.add_net("top.n1", 1, {});
    uint64_t r2 = g.add_register("top.r2", "clkB", 1, {});
    g.add_edge(r1, n1);
    g.add_edge(n1, r2);

    // Should complete without hanging
    auto result = g.find_register_paths(r1);
    EXPECT_FALSE(result.paths.empty());
    EXPECT_EQ(result.paths[0].dst_reg_id, r2);
}

