#include "cdc/cdc006.h"
#include "cdc/synchronizer.h"
#include "ir/graph.h"
#include "clock/domain.h"
#include <gtest/gtest.h>

using namespace opencdc::ir;
using namespace opencdc::clock;
using namespace opencdc::cdc;

class Cdc006Test : public ::testing::Test {
protected:
    Graph graph;
    Cdc006Analyzer analyzer;

    std::vector<ClockDomain> make_domains() {
        std::vector<ClockDomain> d;
        ClockDomain da; da.id = 1; da.name = "clk_a";
        ClockDomain db; db.id = 2; db.name = "clk_b";
        d.push_back(da);
        d.push_back(db);
        return d;
    }
};

TEST_F(Cdc006Test, NoFinding_NoSyncChain) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"", 5, 5});
    uint64_t dst = graph.add_register("mod.dst", "clk_b", 1, {"", 6, 5});
    auto* s = graph.find_node_mutable(src); s->reset_signal = "rst_n";
    auto* d = graph.find_node_mutable(dst); d->reset_signal = "rst_n";
    graph.add_edge(src, dst);

    auto findings = analyzer.analyze(graph, make_domains(), {});
    EXPECT_TRUE(findings.empty());
}

TEST_F(Cdc006Test, NoFinding_DirectRegisterFeed) {
    uint64_t src = graph.add_register("mod.src_ff", "clk_a", 1, {"", 5, 5});
    uint64_t meta = graph.add_register("mod.dst_meta", "clk_b", 1, {"", 8, 5});
    uint64_t sync = graph.add_register("mod.dst_sync", "clk_b", 1, {"", 9, 5});
    auto* s = graph.find_node_mutable(src); s->reset_signal = "rst_n";
    auto* m = graph.find_node_mutable(meta); m->reset_signal = "rst_n";
    auto* y = graph.find_node_mutable(sync); y->reset_signal = "rst_n";
    graph.add_edge(src, meta);
    graph.add_edge(meta, sync);

    auto findings = analyzer.analyze(graph, make_domains(), {});
    EXPECT_TRUE(findings.empty());
}

TEST_F(Cdc006Test, Finding_CombinationalBetweenStages) {
    uint64_t src = graph.add_register("mod.src_ff", "clk_a", 1, {"", 5, 5});
    uint64_t meta = graph.add_register("mod.dst_meta", "clk_b", 1, {"", 8, 5});
    uint64_t sync = graph.add_register("mod.dst_sync", "clk_b", 1, {"", 9, 5});
    uint64_t extra = graph.add_register("mod.extra_reg", "clk_b", 1, {"", 10, 5});
    uint64_t comb = graph.add_combinational("mod.comb_logic", LogicType::And, {extra, meta}, 1, {"", 11, 1});
    auto* s = graph.find_node_mutable(src); s->reset_signal = "rst_n";
    auto* m = graph.find_node_mutable(meta); m->reset_signal = "rst_n";
    auto* y = graph.find_node_mutable(sync); y->reset_signal = "rst_n";
    graph.add_edge(src, meta);
    graph.add_edge(meta, sync);
    graph.add_edge(comb, sync);

    auto findings = analyzer.analyze(graph, make_domains(), {});
    ASSERT_EQ(findings.size(), 1u);
    EXPECT_EQ(findings[0].rule_id, "CDC006");
}

TEST_F(Cdc006Test, NoFinding_SyncChainHasSinglePred) {
    uint64_t src = graph.add_register("mod.src_ff", "clk_a", 1, {"", 5, 5});
    uint64_t meta = graph.add_register("mod.dst_meta", "clk_b", 1, {"", 8, 5});
    uint64_t sync = graph.add_register("mod.dst_sync", "clk_b", 1, {"", 9, 5});
    uint64_t dst = graph.add_register("mod.dst_ff", "clk_b", 1, {"", 10, 5});
    for (uint64_t id : {src, meta, sync, dst}) {
        auto* n = graph.find_node_mutable(id); n->reset_signal = "rst_n";
    }
    graph.add_edge(src, meta);
    graph.add_edge(meta, sync);
    graph.add_edge(sync, dst);

    auto findings = analyzer.analyze(graph, make_domains(), {});
    EXPECT_TRUE(findings.empty());
}

TEST_F(Cdc006Test, NoFinding_MultiPredNotSyncStage) {
    uint64_t src = graph.add_register("mod.src_ff", "clk_a", 1, {"", 5, 5});
    uint64_t dst1 = graph.add_register("mod.dst1_ff", "clk_a", 1, {"", 6, 5});
    uint64_t dst2 = graph.add_register("mod.dst2_ff", "clk_a", 1, {"", 7, 5});
    uint64_t consumer = graph.add_register("mod.consumer_ff", "clk_b", 1, {"", 8, 5});
    for (uint64_t id : {src, dst1, dst2, consumer}) {
        auto* n = graph.find_node_mutable(id); n->reset_signal = "rst_n";
    }
    graph.add_edge(src, consumer);
    graph.add_edge(dst1, consumer);
    graph.add_edge(dst2, consumer);

    auto findings = analyzer.analyze(graph, make_domains(), {});
    EXPECT_TRUE(findings.empty());
}
