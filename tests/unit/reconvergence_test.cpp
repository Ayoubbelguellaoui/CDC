#include "cdc/reconvergence.h"
#include "cdc/crossing.h"
#include "ir/graph.h"
#include "clock/domain.h"
#include <gtest/gtest.h>

using namespace opencdc::ir;
using namespace opencdc::clock;
using namespace opencdc::cdc;

class ReconvergenceTest : public ::testing::Test {
protected:
    Graph graph;
    DomainExtractor domain_extractor;
    CrossingAnalyzer crossing_analyzer;
    ReconvergenceAnalyzer reconvergence_analyzer;
};

TEST_F(ReconvergenceTest, NoReconvergenceSinglePath) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t dst = graph.add_register("mod.dst", "clk_b", 1, {"mod.sv", 6, 5});
    graph.add_edge(src, dst);

    auto dr = domain_extractor.extract(graph);
    auto crossings = crossing_analyzer.analyze(graph, dr.domains, dr.register_to_domain);
    auto reconv = reconvergence_analyzer.analyze(graph, dr.domains, crossings);

    EXPECT_TRUE(reconv.empty());
}

TEST_F(ReconvergenceTest, ReconvergenceDetected) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 2, {"mod.sv", 5, 5});
    uint64_t dst1 = graph.add_register("mod.dst1", "clk_b", 1, {"mod.sv", 8, 5});
    uint64_t dst2 = graph.add_register("mod.dst2", "clk_b", 1, {"mod.sv", 9, 5});
    uint64_t consumer = graph.add_register("mod.consumer", "clk_b", 1, {"mod.sv", 10, 5});

    graph.add_edge(src, dst1);
    graph.add_edge(src, dst2);
    graph.add_edge(dst1, consumer);
    graph.add_edge(dst2, consumer);

    auto dr = domain_extractor.extract(graph);
    auto crossings = crossing_analyzer.analyze(graph, dr.domains, dr.register_to_domain);
    auto reconv = reconvergence_analyzer.analyze(graph, dr.domains, crossings);

    ASSERT_GE(reconv.size(), 1u);
    EXPECT_EQ(reconv[0].rule_id, "CDC003");
    EXPECT_TRUE(reconv[0].reconvergence.is_reconvergent);
    EXPECT_TRUE(reconv[0].reconvergence.is_hazardous);
}

TEST_F(ReconvergenceTest, SafeSingleBitNoHazard) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t dst1 = graph.add_register("mod.dst1", "clk_b", 1, {"mod.sv", 8, 5});
    uint64_t dst2 = graph.add_register("mod.dst2", "clk_b", 1, {"mod.sv", 9, 5});
    uint64_t consumer = graph.add_register("mod.consumer", "clk_b", 1, {"mod.sv", 10, 5});

    graph.add_edge(src, dst1);
    graph.add_edge(src, dst2);
    graph.add_edge(dst1, consumer);
    graph.add_edge(dst2, consumer);

    auto dr = domain_extractor.extract(graph);
    auto crossings = crossing_analyzer.analyze(graph, dr.domains, dr.register_to_domain);
    auto reconv = reconvergence_analyzer.analyze(graph, dr.domains, crossings);

    ASSERT_GE(reconv.size(), 1u);
    EXPECT_FALSE(reconv[0].reconvergence.is_hazardous);
}

TEST_F(ReconvergenceTest, SyncedPathsNotReconvergent) {
    // Single-bit source through independent 2-stage sync chains: the sync
    // chains make the reconvergence safe, so no CDC003 is expected.
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t meta1 = graph.add_register("mod.meta1", "clk_b", 1, {"mod.sv", 8, 5});
    uint64_t sync1 = graph.add_register("mod.sync1", "clk_b", 1, {"mod.sv", 9, 5});
    uint64_t meta2 = graph.add_register("mod.meta2", "clk_b", 1, {"mod.sv", 10, 5});
    uint64_t sync2 = graph.add_register("mod.sync2", "clk_b", 1, {"mod.sv", 11, 5});
    uint64_t consumer = graph.add_register("mod.consumer", "clk_b", 1, {"mod.sv", 12, 5});

    graph.add_edge(src, meta1);
    graph.add_edge(meta1, sync1);
    graph.add_edge(src, meta2);
    graph.add_edge(meta2, sync2);
    graph.add_edge(sync1, consumer);
    graph.add_edge(sync2, consumer);

    auto dr = domain_extractor.extract(graph);
    auto crossings = crossing_analyzer.analyze(graph, dr.domains, dr.register_to_domain);
    auto reconv = reconvergence_analyzer.analyze(graph, dr.domains, crossings);

    EXPECT_TRUE(reconv.empty());
}

TEST_F(ReconvergenceTest, OnlyCDC001CrossingsFeedReconvergence) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 2, {"mod.sv", 5, 5});
    uint64_t dst1 = graph.add_register("mod.dst1", "clk_b", 1, {"mod.sv", 8, 5});
    uint64_t dst2 = graph.add_register("mod.dst2", "clk_b", 1, {"mod.sv", 9, 5});
    uint64_t consumer = graph.add_register("mod.consumer", "clk_b", 1, {"mod.sv", 10, 5});

    auto* nsrc = graph.find_node_mutable(src); nsrc->reset_signal = "rst_n";
    for (auto id : {dst1, dst2, consumer}) {
        auto* n = graph.find_node_mutable(id); n->reset_signal = "rst_n";
    }

    graph.add_edge(src, dst1);
    graph.add_edge(src, dst2);
    graph.add_edge(dst1, consumer);
    graph.add_edge(dst2, consumer);

    auto dr = domain_extractor.extract(graph);
    auto crossings = crossing_analyzer.analyze(graph, dr.domains, dr.register_to_domain);
    auto reconv = reconvergence_analyzer.analyze(graph, dr.domains, crossings);

    ASSERT_GE(reconv.size(), 1u);
    EXPECT_EQ(reconv[0].rule_id, "CDC003");
}

TEST_F(ReconvergenceTest, SyncedMultiBitStillReconvergent) {
    // Multi-bit source through independent sync chains: synchronizers do not
    // make this safe (bits skew independently), so CDC003 must be reported
    // and marked hazardous.
    uint64_t src = graph.add_register("mod.src", "clk_a", 2, {"mod.sv", 5, 5});
    uint64_t meta1 = graph.add_register("mod.meta1", "clk_b", 1, {"mod.sv", 8, 5});
    uint64_t sync1 = graph.add_register("mod.sync1", "clk_b", 1, {"mod.sv", 9, 5});
    uint64_t meta2 = graph.add_register("mod.meta2", "clk_b", 1, {"mod.sv", 10, 5});
    uint64_t sync2 = graph.add_register("mod.sync2", "clk_b", 1, {"mod.sv", 11, 5});
    uint64_t consumer = graph.add_register("mod.consumer", "clk_b", 1, {"mod.sv", 12, 5});

    graph.add_edge(src, meta1);
    graph.add_edge(meta1, sync1);
    graph.add_edge(src, meta2);
    graph.add_edge(meta2, sync2);
    graph.add_edge(sync1, consumer);
    graph.add_edge(sync2, consumer);

    auto dr = domain_extractor.extract(graph);
    auto crossings = crossing_analyzer.analyze(graph, dr.domains, dr.register_to_domain);
    auto reconv = reconvergence_analyzer.analyze(graph, dr.domains, crossings);

    ASSERT_GE(reconv.size(), 1u);
    EXPECT_EQ(reconv[0].rule_id, "CDC003");
    EXPECT_TRUE(reconv[0].reconvergence.is_hazardous);
}
