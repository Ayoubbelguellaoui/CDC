#include "cdc/crossing.h"
#include "ir/graph.h"
#include "clock/domain.h"
#include <gtest/gtest.h>

using namespace opencdc::ir;
using namespace opencdc::clock;
using namespace opencdc::cdc;

class CrossingTest : public ::testing::Test {
protected:
    Graph graph;
    DomainExtractor domain_extractor;
    CrossingAnalyzer crossing_analyzer;
};

TEST_F(CrossingTest, NoCrossingSameDomain) {
    graph.add_port("clk_a", 1, {"", 1, 1});
    uint64_t ff_a = graph.add_register("mod.ff_a", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t ff_b = graph.add_register("mod.ff_b", "clk_a", 1, {"mod.sv", 6, 5});

    graph.add_edge(ff_a, ff_b);

    auto dom_result = domain_extractor.extract(graph);
    auto findings = crossing_analyzer.analyze(graph, dom_result.domains, dom_result.register_to_domain);

    EXPECT_TRUE(findings.empty());
}

TEST_F(CrossingTest, SingleCrossingDetected) {
    uint64_t ff_a = graph.add_register("mod.src_ff", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t ff_b = graph.add_register("mod.dst_ff", "clk_b", 1, {"mod.sv", 6, 5});
    auto* na = graph.find_node_mutable(ff_a);
    na->reset_signal = "rst_n";
    auto* nb = graph.find_node_mutable(ff_b);
    nb->reset_signal = "rst_n";

    graph.add_edge(ff_a, ff_b);

    auto dom_result = domain_extractor.extract(graph);
    auto findings = crossing_analyzer.analyze(graph, dom_result.domains, dom_result.register_to_domain);

    ASSERT_EQ(findings.size(), 1u);
    EXPECT_EQ(findings[0].rule_id, "CDC001");
    EXPECT_EQ(findings[0].severity, "error");
    EXPECT_EQ(findings[0].source_reg_name, "mod.src_ff");
    EXPECT_EQ(findings[0].dest_reg_name, "mod.dst_ff");
    EXPECT_EQ(findings[0].source_domain, "clk_a");
    EXPECT_EQ(findings[0].dest_domain, "clk_b");
}

TEST_F(CrossingTest, CrossingPathRecorded) {
    uint64_t ff_a = graph.add_register("mod.src_ff", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t ff_b = graph.add_register("mod.dst_ff", "clk_b", 1, {"mod.sv", 6, 5});
    auto* na = graph.find_node_mutable(ff_a);
    na->reset_signal = "rst_n";
    auto* nb = graph.find_node_mutable(ff_b);
    nb->reset_signal = "rst_n";

    graph.add_edge(ff_a, ff_b);

    auto dom_result = domain_extractor.extract(graph);
    auto findings = crossing_analyzer.analyze(graph, dom_result.domains, dom_result.register_to_domain);

    ASSERT_EQ(findings.size(), 1u);
    EXPECT_EQ(findings[0].path.node_ids.size(), 2u);
    EXPECT_EQ(findings[0].path.node_ids[0], ff_a);
    EXPECT_EQ(findings[0].path.node_ids[1], ff_b);
}

TEST_F(CrossingTest, FindingHasRequiredFields) {
    uint64_t ff_a = graph.add_register("mod.src_ff", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t ff_b = graph.add_register("mod.dst_ff", "clk_b", 1, {"mod.sv", 6, 5});
    auto* na = graph.find_node_mutable(ff_a);
    na->reset_signal = "rst_n";
    auto* nb = graph.find_node_mutable(ff_b);
    nb->reset_signal = "rst_n";

    graph.add_edge(ff_a, ff_b);

    auto dom_result = domain_extractor.extract(graph);
    auto findings = crossing_analyzer.analyze(graph, dom_result.domains, dom_result.register_to_domain);

    ASSERT_EQ(findings.size(), 1u);
    EXPECT_FALSE(findings[0].rule_id.empty());
    EXPECT_FALSE(findings[0].severity.empty());
    EXPECT_FALSE(findings[0].reason.empty());
    EXPECT_FALSE(findings[0].source_reg_name.empty());
    EXPECT_FALSE(findings[0].dest_reg_name.empty());
    EXPECT_FALSE(findings[0].source_domain.empty());
    EXPECT_FALSE(findings[0].dest_domain.empty());
}

TEST_F(CrossingTest, SyncChainDowngradesCdc001) {
    uint64_t src_ff = graph.add_register("mod.src_ff", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t meta = graph.add_register("mod.meta", "clk_b", 1, {"mod.sv", 8, 5});
    uint64_t sync = graph.add_register("mod.sync", "clk_b", 1, {"mod.sv", 9, 5});
    uint64_t dst = graph.add_register("mod.dst_ff", "clk_b", 1, {"mod.sv", 10, 5});

    for (uint64_t id : {src_ff, meta, sync, dst}) {
        auto* n = graph.find_node_mutable(id);
        n->reset_signal = "rst_n";
    }

    graph.add_edge(src_ff, meta);
    graph.add_edge(meta, sync);
    graph.add_edge(sync, dst);

    auto dom_result = domain_extractor.extract(graph);
    auto findings = crossing_analyzer.analyze(graph, dom_result.domains, dom_result.register_to_domain);

    // CDC001 fires but downgraded to warning when sync detected
    // Derived rules (002/004/005/007) are suppressed
    ASSERT_EQ(findings.size(), 1u);
    EXPECT_EQ(findings[0].rule_id, "CDC001");
    EXPECT_EQ(findings[0].severity, "warning");
    EXPECT_NE(findings[0].detected_sync, SyncPattern::None);
}

TEST_F(CrossingTest, UnsyncedCrossingIsError) {
    uint64_t ff_a = graph.add_register("mod.src_ff", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t ff_b = graph.add_register("mod.dst_ff", "clk_b", 1, {"mod.sv", 6, 5});
    auto* na = graph.find_node_mutable(ff_a);
    na->reset_signal = "rst_n";
    auto* nb = graph.find_node_mutable(ff_b);
    nb->reset_signal = "rst_n";

    graph.add_edge(ff_a, ff_b);

    auto dom_result = domain_extractor.extract(graph);
    auto findings = crossing_analyzer.analyze(graph, dom_result.domains, dom_result.register_to_domain);

    ASSERT_EQ(findings.size(), 1u);
    EXPECT_EQ(findings[0].rule_id, "CDC001");
    EXPECT_EQ(findings[0].severity, "error");
    EXPECT_EQ(findings[0].detected_sync, SyncPattern::None);
}

TEST_F(CrossingTest, TwoCrossingsDetected) {
    uint64_t src1 = graph.add_register("mod.src1", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t dst1 = graph.add_register("mod.dst1", "clk_b", 1, {"mod.sv", 6, 5});
    uint64_t src2 = graph.add_register("mod.src2", "clk_a", 1, {"mod.sv", 8, 5});
    uint64_t dst2 = graph.add_register("mod.dst2", "clk_b", 1, {"mod.sv", 9, 5});

    for (uint64_t id : {src1, dst1, src2, dst2}) {
        auto* n = graph.find_node_mutable(id);
        n->reset_signal = "rst_n";
    }

    graph.add_edge(src1, dst1);
    graph.add_edge(src2, dst2);

    auto dom_result = domain_extractor.extract(graph);
    auto findings = crossing_analyzer.analyze(graph, dom_result.domains, dom_result.register_to_domain);

    EXPECT_EQ(findings.size(), 2u);
}

TEST_F(CrossingTest, HandshakeRegDetected) {
    uint64_t src = graph.add_register("mod.src_ff", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t hs = graph.add_register("mod.req_reg", "clk_b", 1, {"mod.sv", 6, 5});
    uint64_t dst = graph.add_register("mod.dst_ff", "clk_b", 1, {"mod.sv", 7, 5});
    for (uint64_t id : {src, hs, dst}) {
        auto* n = graph.find_node_mutable(id); n->reset_signal = "rst_n";
    }
    auto* hs_node = graph.find_node_mutable(hs);
    hs_node->is_handshake_signal = true;
    graph.add_edge(src, hs);
    graph.add_edge(hs, dst);

    auto dr = domain_extractor.extract(graph);
    auto findings = crossing_analyzer.analyze(graph, dr.domains, dr.register_to_domain);

    ASSERT_EQ(findings.size(), 1u);
    EXPECT_TRUE(findings[0].has_handshake);
}

TEST_F(CrossingTest, HandshakeInstanceDetected) {
    uint64_t src = graph.add_register("mod.src_ff", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t hs = graph.add_register("mod.u_hs.data_reg", "clk_b", 1, {"mod.sv", 6, 5});
    uint64_t dst = graph.add_register("mod.dst_ff", "clk_b", 1, {"mod.sv", 7, 5});
    for (uint64_t id : {src, hs, dst}) {
        auto* n = graph.find_node_mutable(id); n->reset_signal = "rst_n";
    }
    auto* hs_node = graph.find_node_mutable(hs);
    hs_node->is_handshake_signal = true;
    graph.add_edge(src, hs);
    graph.add_edge(hs, dst);

    auto dr = domain_extractor.extract(graph);
    auto findings = crossing_analyzer.analyze(graph, dr.domains, dr.register_to_domain);

    ASSERT_EQ(findings.size(), 1u);
    EXPECT_TRUE(findings[0].has_handshake);
}

TEST_F(CrossingTest, MultiBitCrossingNotHandshake) {
    uint64_t src = graph.add_register("mod.src_ff", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t dst = graph.add_register("mod.multi_bit_crossing_reg", "clk_b", 1, {"mod.sv", 6, 5});
    for (uint64_t id : {src, dst}) {
        auto* n = graph.find_node_mutable(id); n->reset_signal = "rst_n";
    }
    graph.add_edge(src, dst);

    auto dr = domain_extractor.extract(graph);
    auto findings = crossing_analyzer.analyze(graph, dr.domains, dr.register_to_domain);

    ASSERT_EQ(findings.size(), 1u);
    EXPECT_FALSE(findings[0].has_handshake);
}

TEST_F(CrossingTest, MuxedClockDetected) {
    uint64_t src = graph.add_register("mod.mux_reg", "clk_muxed", 1, {"mod.sv", 5, 5});
    uint64_t dst = graph.add_register("mod.dst_ff", "clk_b", 1, {"mod.sv", 6, 5});
    auto* s = graph.find_node_mutable(src); s->clock_is_muxed = true;
    auto* d = graph.find_node_mutable(dst); d->reset_signal = "rst_n";
    graph.add_edge(src, dst);

    auto dr = domain_extractor.extract(graph);
    auto findings = crossing_analyzer.analyze(graph, dr.domains, dr.register_to_domain);

    ASSERT_GE(findings.size(), 1u);
    bool found = false;
    for (const auto& f : findings) {
        if (f.rule_id == "CDC005") found = true;
    }
    EXPECT_TRUE(found);
}

TEST_F(CrossingTest, FalsePathSuppressesCrossing) {
    uint64_t src = graph.add_register("mod.src_ff", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t dst = graph.add_register("mod.dst_ff", "clk_b", 1, {"mod.sv", 6, 5});
    auto* s = graph.find_node_mutable(src); s->reset_signal = "rst_n";
    auto* d = graph.find_node_mutable(dst); d->reset_signal = "rst_n";
    graph.add_edge(src, dst);

    auto dr = domain_extractor.extract(graph);
    opencdc::clock::ClockConstraints constraints;
    opencdc::clock::FalsePath fp;
    fp.from_reg = "mod.src_ff";
    fp.to_reg = "mod.dst_ff";
    constraints.false_paths.push_back(fp);
    crossing_analyzer.set_clock_constraints(&constraints);
    auto findings = crossing_analyzer.analyze(graph, dr.domains, dr.register_to_domain);

    EXPECT_TRUE(findings.empty());
}

TEST_F(CrossingTest, FalsePathPartialMatch) {
    uint64_t src = graph.add_register("mod.src_ff", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t dst = graph.add_register("mod.dst_ff", "clk_b", 1, {"mod.sv", 6, 5});
    auto* s = graph.find_node_mutable(src); s->reset_signal = "rst_n";
    auto* d = graph.find_node_mutable(dst); d->reset_signal = "rst_n";
    graph.add_edge(src, dst);

    auto dr = domain_extractor.extract(graph);
    opencdc::clock::ClockConstraints constraints;
    opencdc::clock::FalsePath fp;
    fp.from_reg = "nonexistent";
    fp.to_reg = "dst_ff";
    constraints.false_paths.push_back(fp);
    crossing_analyzer.set_clock_constraints(&constraints);
    auto findings = crossing_analyzer.analyze(graph, dr.domains, dr.register_to_domain);

    EXPECT_EQ(findings.size(), 1u);
}

TEST_F(CrossingTest, SuppressResetCrossings) {
    uint64_t src = graph.add_register("mod.src_ff", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t dst = graph.add_register("mod.dst_ff", "clk_b", 1, {"mod.sv", 6, 5});
    auto* s = graph.find_node_mutable(src); s->reset_signal = "rst_n";
    auto* d = graph.find_node_mutable(dst); d->reset_signal = "rst_n";
    graph.add_edge(src, dst);

    auto dr = domain_extractor.extract(graph);
    auto findings = crossing_analyzer.analyze(graph, dr.domains, dr.register_to_domain);

    ASSERT_EQ(findings.size(), 1u);
    EXPECT_EQ(findings[0].rule_id, "CDC001");
}

TEST_F(CrossingTest, MulticyclePathAnnotatesCrossing) {
    uint64_t src = graph.add_register("mod.src_ff", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t dst = graph.add_register("mod.dst_ff", "clk_b", 1, {"mod.sv", 6, 5});
    auto* s = graph.find_node_mutable(src); s->reset_signal = "rst_n";
    auto* d = graph.find_node_mutable(dst); d->reset_signal = "rst_n";
    graph.add_edge(src, dst);

    auto dr = domain_extractor.extract(graph);
    opencdc::clock::ClockConstraints constraints;
    opencdc::clock::MultiCyclePath mcp;
    mcp.from_clock = "clk_a";
    mcp.to_clock = "clk_b";
    mcp.cycles = 3;
    constraints.multi_cycle_paths.push_back(mcp);
    crossing_analyzer.set_clock_constraints(&constraints);
    auto findings = crossing_analyzer.analyze(graph, dr.domains, dr.register_to_domain);

    ASSERT_EQ(findings.size(), 1u);
    EXPECT_TRUE(findings[0].has_multicycle_exception);
    EXPECT_EQ(findings[0].multicycle_cycles, 3);
}
