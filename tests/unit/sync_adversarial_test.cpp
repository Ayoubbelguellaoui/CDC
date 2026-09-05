#include "cdc/synchronizer.h"
#include "ir/graph.h"
#include <gtest/gtest.h>

using namespace opencdc::ir;
using namespace opencdc::cdc;

class SyncAdversarialTest : public ::testing::Test {
protected:
    Graph graph;
    SynchronizerMatcher matcher;
};

TEST_F(SyncAdversarialTest, MultiBitBusRejectsSyncDetection) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 8, {"mod.sv", 5, 5});
    uint64_t meta = graph.add_register("mod.meta", "clk_b", 8, {"mod.sv", 8, 5});
    uint64_t sync = graph.add_register("mod.sync", "clk_b", 8, {"mod.sv", 9, 5});

    graph.add_edge(src, meta);
    graph.add_edge(meta, sync);

    auto pat = matcher.find_pattern_for_dest(meta, graph);
    EXPECT_EQ(pat, SyncPattern::None);
}

TEST_F(SyncAdversarialTest, SyncRequiresSameClockOnAllStages) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t meta = graph.add_register("mod.meta", "clk_b", 1, {"mod.sv", 8, 5});
    uint64_t sync = graph.add_register("mod.sync", "clk_c", 1, {"mod.sv", 9, 5});

    graph.add_edge(src, meta);
    graph.add_edge(meta, sync);

    auto pat = matcher.find_pattern_for_dest(meta, graph);
    EXPECT_EQ(pat, SyncPattern::None);
}

TEST_F(SyncAdversarialTest, FourFFChainReportedAsFourFF) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t s1 = graph.add_register("mod.s1", "clk_b", 1, {"mod.sv", 8, 5});
    uint64_t s2 = graph.add_register("mod.s2", "clk_b", 1, {"mod.sv", 9, 5});
    uint64_t s3 = graph.add_register("mod.s3", "clk_b", 1, {"mod.sv", 10, 5});
    uint64_t s4 = graph.add_register("mod.s4", "clk_b", 1, {"mod.sv", 11, 5});

    graph.add_edge(src, s1);
    graph.add_edge(s1, s2);
    graph.add_edge(s2, s3);
    graph.add_edge(s3, s4);

    auto pat = matcher.find_pattern_for_dest(s1, graph);
    EXPECT_EQ(pat, SyncPattern::FourFF);
}

TEST_F(SyncAdversarialTest, FiveFFChainReportedAsThreeFF) {
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
    EXPECT_EQ(pat, SyncPattern::ThreeFF);
}

TEST_F(SyncAdversarialTest, StrictModeRejectsSameDomainPred) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t same_domain = graph.add_register("mod.same_domain", "clk_b", 1, {"mod.sv", 6, 5});
    uint64_t meta = graph.add_register("mod.meta", "clk_b", 1, {"mod.sv", 8, 5});
    uint64_t sync = graph.add_register("mod.sync", "clk_b", 1, {"mod.sv", 9, 5});

    graph.add_edge(src, meta);
    graph.add_edge(same_domain, meta);
    graph.add_edge(meta, sync);

    auto pat = matcher.find_pattern_for_dest(meta, graph, true);
    EXPECT_EQ(pat, SyncPattern::None);
}

TEST_F(SyncAdversarialTest, NonStrictModeAllowsSameDomainPred) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t same_domain = graph.add_register("mod.same_domain", "clk_b", 1, {"mod.sv", 6, 5});
    uint64_t meta = graph.add_register("mod.meta", "clk_b", 1, {"mod.sv", 8, 5});
    uint64_t sync = graph.add_register("mod.sync", "clk_b", 1, {"mod.sv", 9, 5});

    graph.add_edge(src, meta);
    graph.add_edge(same_domain, meta);
    graph.add_edge(meta, sync);

    auto pat = matcher.find_pattern_for_dest(meta, graph, false);
    EXPECT_EQ(pat, SyncPattern::TwoFF);
}

TEST_F(SyncAdversarialTest, SingleBitWithWidthOnePasses) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t meta = graph.add_register("mod.meta", "clk_b", 1, {"mod.sv", 8, 5});
    uint64_t sync = graph.add_register("mod.sync", "clk_b", 1, {"mod.sv", 9, 5});

    graph.add_edge(src, meta);
    graph.add_edge(meta, sync);

    auto pat = matcher.find_pattern_for_dest(meta, graph);
    EXPECT_EQ(pat, SyncPattern::TwoFF);
}

TEST_F(SyncAdversarialTest, MissingMiddleStageBreaksChain) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t meta = graph.add_register("mod.meta", "clk_b", 1, {"mod.sv", 8, 5});

    graph.add_edge(src, meta);

    auto pat = matcher.find_pattern_for_dest(meta, graph);
    EXPECT_EQ(pat, SyncPattern::None)
        << "Single stage without same-domain successor should not be detected";
}

TEST_F(SyncAdversarialTest, MismatchedResetPolarityWarning) {
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

    auto chains = matcher.match(graph);
    ASSERT_GE(chains.size(), 1u);
    bool found_polarity_warning = false;
    for (const auto& w : chains[0].warnings) {
        if (w.find("polarity") != std::string::npos)
            found_polarity_warning = true;
    }
    EXPECT_TRUE(found_polarity_warning);
}

TEST_F(SyncAdversarialTest, StrictModeWarningsMatchChain) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t meta = graph.add_register("mod.meta", "clk_b", 1, {"mod.sv", 8, 5});
    uint64_t sync = graph.add_register("mod.sync", "clk_b", 1, {"mod.sv", 9, 5});
    uint64_t extra = graph.add_register("mod.extra", "clk_b", 1, {"mod.sv", 10, 5});

    graph.add_edge(src, meta);
    graph.add_edge(extra, meta);
    graph.add_edge(meta, sync);

    auto pat = matcher.find_pattern_for_dest(meta, graph, true);
    EXPECT_EQ(pat, SyncPattern::None);

    EXPECT_FALSE(matcher.has_chain_warnings(graph, meta));
}

TEST_F(SyncAdversarialTest, CombBetweenSyncStagesBreaksChainDetection) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t meta = graph.add_register("mod.meta", "clk_b", 1, {"mod.sv", 8, 5});
    uint64_t gate = graph.add_combinational("mod.gate", LogicType::And, {meta}, 1,
                                            {"mod.sv", 9, 5});
    uint64_t sync = graph.add_register("mod.sync", "clk_b", 1, {"mod.sv", 10, 5});

    graph.add_edge(src, meta);
    graph.add_edge(meta, gate);
    graph.add_edge(gate, sync);

    auto pat = matcher.find_pattern_for_dest(meta, graph);
    EXPECT_EQ(pat, SyncPattern::None)
        << "Combinational logic between stages should break chain detection";
}

TEST_F(SyncAdversarialTest, FanoutOnStage2TriggersWarning) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t meta = graph.add_register("mod.meta", "clk_b", 1, {"mod.sv", 8, 5});
    uint64_t sync1 = graph.add_register("mod.sync1", "clk_b", 1, {"mod.sv", 9, 5});
    uint64_t sync2 = graph.add_register("mod.sync2", "clk_b", 1, {"mod.sv", 10, 5});
    uint64_t consumer = graph.add_register("mod.consumer", "clk_b", 1, {"mod.sv", 11, 5});

    graph.add_edge(src, meta);
    graph.add_edge(meta, sync1);
    graph.add_edge(sync1, sync2);
    graph.add_edge(sync1, consumer);

    auto chains = matcher.match(graph);
    ASSERT_GE(chains.size(), 1u);
    bool found_fanout_warning = false;
    for (const auto& w : chains[0].warnings) {
        if (w.find("fanout") != std::string::npos)
            found_fanout_warning = true;
    }
    EXPECT_TRUE(found_fanout_warning)
        << "Expected fanout warning from intermediate sync stage";
}

TEST_F(SyncAdversarialTest, DifferentAsyncResetSignalsWarning) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t meta = graph.add_register("mod.meta", "clk_b", 1, {"mod.sv", 8, 5});
    uint64_t sync = graph.add_register("mod.sync", "clk_b", 1, {"mod.sv", 9, 5});

    auto* m = graph.find_node_mutable(meta);
    m->is_async_reset = true;
    m->reset_signal = "arst_n";
    auto* s = graph.find_node_mutable(sync);
    s->is_async_reset = true;
    s->reset_signal = "brst_n";

    graph.add_edge(src, meta);
    graph.add_edge(meta, sync);

    auto chains = matcher.match(graph);
    ASSERT_GE(chains.size(), 1u);
    bool found_diff_reset_warning = false;
    for (const auto& w : chains[0].warnings) {
        if (w.find("Different async reset") != std::string::npos)
            found_diff_reset_warning = true;
    }
    EXPECT_TRUE(found_diff_reset_warning)
        << "Expected warning for different async reset signals between stages";
}
