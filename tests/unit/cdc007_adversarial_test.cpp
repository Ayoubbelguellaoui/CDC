#include "cdc/crossing.h"
#include "ir/graph.h"
#include "clock/domain.h"
#include <gtest/gtest.h>

using namespace opencdc::ir;
using namespace opencdc::cdc;
using namespace opencdc::clock;

class Cdc007AdversarialTest : public ::testing::Test {
protected:
    Graph graph;
    DomainExtractor domain_extractor;
    CrossingAnalyzer crossing_analyzer;
};

TEST_F(Cdc007AdversarialTest, SrcHasResetDstNoResetInfoCdc007) {
    uint64_t src = graph.add_register("mod.src_ff", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t dst = graph.add_register("mod.dst_ff", "clk_b", 1, {"mod.sv", 6, 5});
    auto* s = graph.find_node_mutable(src);
    s->reset_signal = "rst_n";
    graph.add_edge(src, dst);

    auto dr = domain_extractor.extract(graph);
    auto findings = crossing_analyzer.analyze(graph, dr.domains, dr.register_to_domain);

    bool found_warning = false;
    bool found_info = false;
    for (const auto& f : findings) {
        if (f.rule_id == "CDC007" && f.severity == "warning")
            found_warning = true;
        if (f.rule_id == "CDC007" && f.severity == "info")
            found_info = true;
    }
    EXPECT_FALSE(found_warning) << "CDC007 should not fire as warning when source has reset";
    EXPECT_TRUE(found_info) << "CDC007 should fire as info for mixed reset scenario";
}

TEST_F(Cdc007AdversarialTest, SrcNoResetDstHasResetInfoCdc007) {
    uint64_t src = graph.add_register("mod.src_ff", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t dst = graph.add_register("mod.dst_ff", "clk_b", 1, {"mod.sv", 6, 5});
    auto* d = graph.find_node_mutable(dst);
    d->reset_signal = "rst_n";
    graph.add_edge(src, dst);

    auto dr = domain_extractor.extract(graph);
    auto findings = crossing_analyzer.analyze(graph, dr.domains, dr.register_to_domain);

    bool found_warning = false;
    bool found_info = false;
    for (const auto& f : findings) {
        if (f.rule_id == "CDC007" && f.severity == "warning")
            found_warning = true;
        if (f.rule_id == "CDC007" && f.severity == "info")
            found_info = true;
    }
    EXPECT_FALSE(found_warning) << "CDC007 should not fire as warning when destination has reset";
    EXPECT_TRUE(found_info) << "CDC007 should fire as info for mixed reset scenario";
}

TEST_F(Cdc007AdversarialTest, BothNoResetTriggersCdc007) {
    uint64_t src = graph.add_register("mod.src_ff", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t dst = graph.add_register("mod.dst_ff", "clk_b", 1, {"mod.sv", 6, 5});
    graph.add_edge(src, dst);

    auto dr = domain_extractor.extract(graph);
    auto findings = crossing_analyzer.analyze(graph, dr.domains, dr.register_to_domain);

    bool found = false;
    for (const auto& f : findings) {
        if (f.rule_id == "CDC007") found = true;
    }
    EXPECT_TRUE(found);
}

TEST_F(Cdc007AdversarialTest, BothHaveResetNoCdc007) {
    uint64_t src = graph.add_register("mod.src_ff", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t dst = graph.add_register("mod.dst_ff", "clk_b", 1, {"mod.sv", 6, 5});
    auto* s = graph.find_node_mutable(src);
    s->reset_signal = "rst_n";
    auto* d = graph.find_node_mutable(dst);
    d->reset_signal = "rst_n";
    graph.add_edge(src, dst);

    auto dr = domain_extractor.extract(graph);
    auto findings = crossing_analyzer.analyze(graph, dr.domains, dr.register_to_domain);

    for (const auto& f : findings) {
        if (f.rule_id == "CDC007") {
            FAIL() << "CDC007 should not fire when both registers have reset";
        }
    }
}
