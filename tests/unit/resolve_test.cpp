#include <gtest/gtest.h>
#include "clock/resolve.h"
#include "ir/graph.h"

using namespace opencdc::ir;
using namespace opencdc::clock;

class ClockResolverTest : public ::testing::Test {
protected:
    Graph graph;
    ClockResolver resolver;
};

TEST_F(ClockResolverTest, SimpleClockPortResolution) {
    // Port "clk" feeds a Register directly.
    uint64_t clk = graph.add_port("top.clk", 1, {"top.sv", 1, 1});
    uint64_t ff = graph.add_register("top.ff", "top.clk", 1, {"top.sv", 2, 1});
    graph.add_edge(clk, ff);

    auto result = resolver.resolve(graph);
    ASSERT_TRUE(result.clock_map.count("top.clk"));
    // trace_to_top_port returns short name "clk" when unambiguous.
    EXPECT_EQ(result.clock_map["top.clk"].root_clock, "clk");
    EXPECT_FALSE(result.clock_map["top.clk"].is_gated);
}

TEST_F(ClockResolverTest, GatedClockDetection) {
    // Port "clk" → Net with And logic → Register.
    uint64_t clk = graph.add_port("top.clk", 1, {"top.sv", 1, 1});
    uint64_t en = graph.add_port("top.en", 1, {"top.sv", 1, 2});
    uint64_t gating = graph.add_net("top.gating", 1, {"top.sv", 2, 1}, "top");
    auto* gating_node = graph.find_node_mutable(gating);
    gating_node->logic_type = LogicType::And;
    uint64_t ff = graph.add_register("top.ff", "top.clk", 1, {"top.sv", 3, 1});
    graph.add_edge(clk, gating);
    graph.add_edge(en, gating);
    graph.add_edge(gating, ff);

    auto result = resolver.resolve(graph);
    ASSERT_TRUE(result.clock_map.count("top.clk"));
    EXPECT_TRUE(result.clock_map["top.clk"].is_gated);
    EXPECT_EQ(result.clock_map["top.clk"].root_clock, "clk");
}

TEST_F(ClockResolverTest, MuxedClockDetection) {
    // Port "clk" → Net with Mux logic → Register.
    uint64_t clk = graph.add_port("top.clk", 1, {"top.sv", 1, 1});
    uint64_t sel = graph.add_port("top.sel", 1, {"top.sv", 1, 2});
    uint64_t mux = graph.add_net("top.mux", 1, {"top.sv", 2, 1}, "top");
    auto* mux_node = graph.find_node_mutable(mux);
    mux_node->logic_type = LogicType::Mux;
    uint64_t ff = graph.add_register("top.ff", "top.clk", 1, {"top.sv", 3, 1});
    graph.add_edge(clk, mux);
    graph.add_edge(sel, mux);
    graph.add_edge(mux, ff);

    auto result = resolver.resolve(graph);
    ASSERT_TRUE(result.clock_map.count("top.clk"));
    EXPECT_TRUE(result.clock_map["top.clk"].is_muxed);
}

TEST_F(ClockResolverTest, NetChainResolution) {
    // Port "clk" → Net "n1" → Net "n2" → Register.
    uint64_t clk = graph.add_port("top.clk", 1, {"top.sv", 1, 1});
    uint64_t n1 = graph.add_net("top.n1", 1, {"top.sv", 2, 1}, "top");
    uint64_t n2 = graph.add_net("top.n2", 1, {"top.sv", 3, 1}, "top");
    uint64_t ff = graph.add_register("top.ff", "top.clk", 1, {"top.sv", 4, 1});
    graph.add_edge(clk, n1);
    graph.add_edge(n1, n2);
    graph.add_edge(n2, ff);

    auto result = resolver.resolve(graph);
    ASSERT_TRUE(result.clock_map.count("top.clk"));
    // trace_to_top_port returns short name "clk" when unambiguous.
    EXPECT_EQ(result.clock_map["top.clk"].root_clock, "clk");
}

TEST_F(ClockResolverTest, EmptyGraphReturnsEmpty) {
    auto result = resolver.resolve(graph);
    EXPECT_TRUE(result.clock_map.empty());
}

TEST_F(ClockResolverTest, RegisterWithoutClockPortFallsBack) {
    // Register with root_clock already set, no Port node in graph.
    uint64_t ff = graph.add_register("top.ff", "top.clk", 1, {"top.sv", 1, 1});
    auto* node = graph.find_node_mutable(ff);
    node->root_clock = "pre_resolved_clock";

    auto result = resolver.resolve(graph);
    ASSERT_TRUE(result.clock_map.count("top.clk"));
    EXPECT_EQ(result.clock_map["top.clk"].root_clock, "pre_resolved_clock");
}
