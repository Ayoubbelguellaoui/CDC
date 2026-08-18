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
    uint64_t clk_a = graph.add_port("clk_a", 1, {"", 1, 1});
    uint64_t ff_a = graph.add_register("mod.ff_a", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t ff_b = graph.add_register("mod.ff_b", "clk_a", 1, {"mod.sv", 6, 5});

    graph.add_edge(ff_a, ff_b);

    auto dom_result = domain_extractor.extract(graph);
    auto findings = crossing_analyzer.analyze(graph, dom_result.domains);

    EXPECT_TRUE(findings.empty());
}

TEST_F(CrossingTest, SingleCrossingDetected) {
    uint64_t ff_a = graph.add_register("mod.src_ff", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t ff_b = graph.add_register("mod.dst_ff", "clk_b", 1, {"mod.sv", 6, 5});

    graph.add_edge(ff_a, ff_b);

    auto dom_result = domain_extractor.extract(graph);
    auto findings = crossing_analyzer.analyze(graph, dom_result.domains);

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

    graph.add_edge(ff_a, ff_b);

    auto dom_result = domain_extractor.extract(graph);
    auto findings = crossing_analyzer.analyze(graph, dom_result.domains);

    ASSERT_EQ(findings.size(), 1u);
    EXPECT_EQ(findings[0].path.node_ids.size(), 2u);
    EXPECT_EQ(findings[0].path.node_ids[0], ff_a);
    EXPECT_EQ(findings[0].path.node_ids[1], ff_b);
}

TEST_F(CrossingTest, FindingHasRequiredFields) {
    uint64_t ff_a = graph.add_register("mod.src_ff", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t ff_b = graph.add_register("mod.dst_ff", "clk_b", 1, {"mod.sv", 6, 5});

    graph.add_edge(ff_a, ff_b);

    auto dom_result = domain_extractor.extract(graph);
    auto findings = crossing_analyzer.analyze(graph, dom_result.domains);

    ASSERT_EQ(findings.size(), 1u);
    EXPECT_FALSE(findings[0].rule_id.empty());
    EXPECT_FALSE(findings[0].severity.empty());
    EXPECT_FALSE(findings[0].reason.empty());
    EXPECT_FALSE(findings[0].source_reg_name.empty());
    EXPECT_FALSE(findings[0].dest_reg_name.empty());
    EXPECT_FALSE(findings[0].source_domain.empty());
    EXPECT_FALSE(findings[0].dest_domain.empty());
}

TEST_F(CrossingTest, NoCrossingWhenSyncChainExists) {
    uint64_t src_ff = graph.add_register("mod.src_ff", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t meta = graph.add_register("mod.meta", "clk_b", 1, {"mod.sv", 8, 5});
    uint64_t sync = graph.add_register("mod.sync", "clk_b", 1, {"mod.sv", 9, 5});
    uint64_t dst = graph.add_register("mod.dst_ff", "clk_b", 1, {"mod.sv", 10, 5});

    graph.add_edge(src_ff, meta);
    graph.add_edge(meta, sync);
    graph.add_edge(sync, dst);

    auto dom_result = domain_extractor.extract(graph);
    auto findings = crossing_analyzer.analyze(graph, dom_result.domains);

    ASSERT_EQ(findings.size(), 1u);
    EXPECT_NE(findings[0].detected_sync, SyncPattern::None);
}

TEST_F(CrossingTest, TwoCrossingsDetected) {
    uint64_t src1 = graph.add_register("mod.src1", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t dst1 = graph.add_register("mod.dst1", "clk_b", 1, {"mod.sv", 6, 5});
    uint64_t src2 = graph.add_register("mod.src2", "clk_a", 1, {"mod.sv", 8, 5});
    uint64_t dst2 = graph.add_register("mod.dst2", "clk_b", 1, {"mod.sv", 9, 5});

    graph.add_edge(src1, dst1);
    graph.add_edge(src2, dst2);

    auto dom_result = domain_extractor.extract(graph);
    auto findings = crossing_analyzer.analyze(graph, dom_result.domains);

    EXPECT_EQ(findings.size(), 2u);
}
