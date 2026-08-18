#include "cdc/synchronizer.h"
#include "cdc/crossing.h"
#include "ir/graph.h"
#include "clock/domain.h"
#include <gtest/gtest.h>

using namespace opencdc::ir;
using namespace opencdc::clock;
using namespace opencdc::cdc;

class SyncTest : public ::testing::Test {
protected:
    Graph graph;
    SynchronizerMatcher matcher;
};

TEST_F(SyncTest, Detect2FFChain) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t meta = graph.add_register("mod.meta", "clk_b", 1, {"mod.sv", 8, 5});
    uint64_t sync = graph.add_register("mod.sync", "clk_b", 1, {"mod.sv", 9, 5});

    graph.add_edge(src, meta);
    graph.add_edge(meta, sync);

    auto pat = matcher.find_pattern_for_dest(meta, graph);
    EXPECT_EQ(pat, SyncPattern::TwoFF);
}

TEST_F(SyncTest, Detect3FFChain) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t m1 = graph.add_register("mod.m1", "clk_b", 1, {"mod.sv", 8, 5});
    uint64_t m2 = graph.add_register("mod.m2", "clk_b", 1, {"mod.sv", 9, 5});
    uint64_t sync = graph.add_register("mod.sync", "clk_b", 1, {"mod.sv", 10, 5});

    graph.add_edge(src, m1);
    graph.add_edge(m1, m2);
    graph.add_edge(m2, sync);

    auto pat = matcher.find_pattern_for_dest(m1, graph);
    EXPECT_EQ(pat, SyncPattern::ThreeFF);
}

TEST_F(SyncTest, RejectChainWithDifferentClocks) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t meta = graph.add_register("mod.meta", "clk_b", 1, {"mod.sv", 8, 5});
    uint64_t sync = graph.add_register("mod.sync", "clk_c", 1, {"mod.sv", 9, 5});

    graph.add_edge(src, meta);
    graph.add_edge(meta, sync);

    auto pat = matcher.find_pattern_for_dest(meta, graph);
    EXPECT_EQ(pat, SyncPattern::None);
}

TEST_F(SyncTest, NoChainReturnsNone) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t dst = graph.add_register("mod.dst", "clk_b", 1, {"mod.sv", 6, 5});

    graph.add_edge(src, dst);

    auto pat = matcher.find_pattern_for_dest(dst, graph);
    EXPECT_EQ(pat, SyncPattern::None);
}

TEST_F(SyncTest, MatchFindsChains) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t meta = graph.add_register("mod.meta", "clk_b", 1, {"mod.sv", 8, 5});
    uint64_t sync = graph.add_register("mod.sync", "clk_b", 1, {"mod.sv", 9, 5});

    graph.add_edge(src, meta);
    graph.add_edge(meta, sync);

    auto chains = matcher.match(graph);
    ASSERT_EQ(chains.size(), 1u);
    EXPECT_EQ(chains[0].pattern, SyncPattern::TwoFF);
    EXPECT_EQ(chains[0].source_reg_id, src);
    EXPECT_EQ(chains[0].stage_ids.size(), 2u);
}
