#include <gtest/gtest.h>

#include "cdc/synchronizer.h"
#include "ir/graph.h"

using namespace opencdc::ir;
using namespace opencdc::cdc;

class SyncComprehensiveTest : public ::testing::Test {
   protected:
    Graph graph;
    SynchronizerMatcher matcher;
};

TEST_F(SyncComprehensiveTest, HasChainWarningsReturnsTrueWhenWarningsExist) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t meta = graph.add_register("mod.meta", "clk_b", 1, {"mod.sv", 8, 5});
    uint64_t sync = graph.add_register("mod.sync", "clk_b", 1, {"mod.sv", 9, 5});

    auto* m = graph.find_node_mutable(meta);
    m->reset_signal = "rst_n";
    m->reset_pol = ResetPolarity::ActiveLow;
    auto* s = graph.find_node_mutable(sync);
    s->reset_signal = "rst";
    s->reset_pol = ResetPolarity::ActiveHigh;

    graph.add_edge(src, meta);
    graph.add_edge(meta, sync);

    EXPECT_TRUE(matcher.has_chain_warnings(graph, meta));
}

TEST_F(SyncComprehensiveTest, HasChainWarningsNonStrictAllowsSameDomainPred) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t same_domain = graph.add_register("mod.same_domain", "clk_b", 1, {"mod.sv", 6, 5});
    uint64_t meta = graph.add_register("mod.meta", "clk_b", 1, {"mod.sv", 8, 5});
    uint64_t sync = graph.add_register("mod.sync", "clk_b", 1, {"mod.sv", 9, 5});

    graph.add_edge(src, meta);
    graph.add_edge(same_domain, meta);
    graph.add_edge(meta, sync);

    // Non-strict: same-domain pred should not prevent walking the chain
    EXPECT_FALSE(matcher.has_chain_warnings(graph, meta, false));
}

TEST_F(SyncComprehensiveTest, HasChainWarningsStrictRejectsSameDomainPred) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t same_domain = graph.add_register("mod.same_domain", "clk_b", 1, {"mod.sv", 6, 5});
    uint64_t meta = graph.add_register("mod.meta", "clk_b", 1, {"mod.sv", 8, 5});
    uint64_t sync = graph.add_register("mod.sync", "clk_b", 1, {"mod.sv", 9, 5});

    graph.add_edge(src, meta);
    graph.add_edge(same_domain, meta);
    graph.add_edge(meta, sync);

    // Strict: same-domain pred blocks chain detection
    EXPECT_FALSE(matcher.has_chain_warnings(graph, meta, true));
}

TEST_F(SyncComprehensiveTest, AsyncResetMetastabilityWarning) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t meta = graph.add_register("mod.meta", "clk_b", 1, {"mod.sv", 8, 5});
    uint64_t sync = graph.add_register("mod.sync", "clk_b", 1, {"mod.sv", 9, 5});

    // sync has async reset but meta doesn't — triggers metastability warning
    auto* s = graph.find_node_mutable(sync);
    s->is_async_reset = true;
    s->reset_signal = "arst_n";

    graph.add_edge(src, meta);
    graph.add_edge(meta, sync);

    auto chains = matcher.match(graph);
    ASSERT_GE(chains.size(), 1u);
    bool found = false;
    for (const auto& w : chains[0].warnings) {
        if (w.find("Asynchronous reset") != std::string::npos)
            found = true;
    }
    EXPECT_TRUE(found);
}

TEST_F(SyncComprehensiveTest, SyncAfterAsyncWarning) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t meta = graph.add_register("mod.meta", "clk_b", 1, {"mod.sv", 8, 5});
    uint64_t sync = graph.add_register("mod.sync", "clk_b", 1, {"mod.sv", 9, 5});

    auto* m = graph.find_node_mutable(meta);
    m->is_async_reset = true;
    m->reset_signal = "arst_n";
    auto* s = graph.find_node_mutable(sync);
    s->is_async_reset = false;
    s->reset_signal = "arst_n";

    graph.add_edge(src, meta);
    graph.add_edge(meta, sync);

    auto chains = matcher.match(graph);
    ASSERT_GE(chains.size(), 1u);
    bool found = false;
    for (const auto& w : chains[0].warnings) {
        if (w.find("Synchronous reset") != std::string::npos &&
            w.find("after async") != std::string::npos)
            found = true;
    }
    EXPECT_TRUE(found);
}

TEST_F(SyncComprehensiveTest, ThreeStageResetValidation) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t s1 = graph.add_register("mod.s1", "clk_b", 1, {"mod.sv", 8, 5});
    uint64_t s2 = graph.add_register("mod.s2", "clk_b", 1, {"mod.sv", 9, 5});
    uint64_t s3 = graph.add_register("mod.s3", "clk_b", 1, {"mod.sv", 10, 5});

    auto* n1 = graph.find_node_mutable(s1);
    n1->reset_signal = "rst_a";
    n1->reset_pol = ResetPolarity::ActiveHigh;
    auto* n2 = graph.find_node_mutable(s2);
    n2->reset_signal = "rst_a";
    n2->reset_pol = ResetPolarity::ActiveHigh;
    auto* n3 = graph.find_node_mutable(s3);
    n3->reset_signal = "rst_b";
    n3->reset_pol = ResetPolarity::ActiveLow;

    graph.add_edge(src, s1);
    graph.add_edge(s1, s2);
    graph.add_edge(s2, s3);

    auto chains = matcher.match(graph);
    ASSERT_GE(chains.size(), 1u);
    bool found_polarity = false;
    for (const auto& w : chains[0].warnings) {
        if (w.find("polarity") != std::string::npos)
            found_polarity = true;
    }
    EXPECT_TRUE(found_polarity);
}

TEST_F(SyncComprehensiveTest, MatchDeduplicatesSameChain) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t meta = graph.add_register("mod.meta", "clk_b", 1, {"mod.sv", 8, 5});
    uint64_t sync = graph.add_register("mod.sync", "clk_b", 1, {"mod.sv", 9, 5});

    // Two paths into meta from same source (e.g. through different combinational logic)
    // but same register source — dedup should collapse to one chain.
    graph.add_edge(src, meta);
    graph.add_edge(meta, sync);

    auto chains = matcher.match(graph);
    EXPECT_EQ(chains.size(), 1u);
}

TEST_F(SyncComprehensiveTest, DepthFiveReturnsNStage) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t s1 = graph.add_register("mod.s1", "clk_b", 1, {"mod.sv", 8, 5});
    uint64_t s2 = graph.add_register("mod.s2", "clk_b", 1, {"mod.sv", 9, 5});
    uint64_t s3 = graph.add_register("mod.s3", "clk_b", 1, {"mod.sv", 10, 5});
    uint64_t s4 = graph.add_register("mod.s4", "clk_b", 1, {"mod.sv", 11, 5});
    uint64_t s5 = graph.add_register("mod.s5", "clk_b", 1, {"mod.sv", 12, 5});

    graph.add_edge(src, s1);
    graph.add_edge(s1, s2);
    graph.add_edge(s2, s3);
    graph.add_edge(s3, s4);
    graph.add_edge(s4, s5);

    auto pat = matcher.find_pattern_for_dest(s1, graph);
    EXPECT_EQ(pat, SyncPattern::NStage);
}

TEST_F(SyncComprehensiveTest, DepthSevenReturnsNStage) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t s1 = graph.add_register("mod.s1", "clk_b", 1, {"mod.sv", 8, 5});
    uint64_t s2 = graph.add_register("mod.s2", "clk_b", 1, {"mod.sv", 9, 5});
    uint64_t s3 = graph.add_register("mod.s3", "clk_b", 1, {"mod.sv", 10, 5});
    uint64_t s4 = graph.add_register("mod.s4", "clk_b", 1, {"mod.sv", 11, 5});
    uint64_t s5 = graph.add_register("mod.s5", "clk_b", 1, {"mod.sv", 12, 5});
    uint64_t s6 = graph.add_register("mod.s6", "clk_b", 1, {"mod.sv", 13, 5});
    uint64_t s7 = graph.add_register("mod.s7", "clk_b", 1, {"mod.sv", 14, 5});

    graph.add_edge(src, s1);
    graph.add_edge(s1, s2);
    graph.add_edge(s2, s3);
    graph.add_edge(s3, s4);
    graph.add_edge(s4, s5);
    graph.add_edge(s5, s6);
    graph.add_edge(s6, s7);

    auto pat = matcher.find_pattern_for_dest(s1, graph);
    EXPECT_EQ(pat, SyncPattern::NStage);
}

TEST_F(SyncComprehensiveTest, DepthFiveThroughMatchReturnsNStage) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t s1 = graph.add_register("mod.s1", "clk_b", 1, {"mod.sv", 8, 5});
    uint64_t s2 = graph.add_register("mod.s2", "clk_b", 1, {"mod.sv", 9, 5});
    uint64_t s3 = graph.add_register("mod.s3", "clk_b", 1, {"mod.sv", 10, 5});
    uint64_t s4 = graph.add_register("mod.s4", "clk_b", 1, {"mod.sv", 11, 5});
    uint64_t s5 = graph.add_register("mod.s5", "clk_b", 1, {"mod.sv", 12, 5});

    graph.add_edge(src, s1);
    graph.add_edge(s1, s2);
    graph.add_edge(s2, s3);
    graph.add_edge(s3, s4);
    graph.add_edge(s4, s5);

    auto chains = matcher.match(graph);
    ASSERT_EQ(chains.size(), 1u);
    EXPECT_EQ(chains[0].pattern, SyncPattern::NStage);
    EXPECT_EQ(chains[0].depth, 5u);
}
