#include <gtest/gtest.h>
#include "ir/graph.h"

using namespace opencdc::ir;

TEST(GraphTest, AddRegister) {
    Graph g;
    uint64_t id = g.add_register("mod.ff1", "clk", 1, {});
    EXPECT_EQ(g.register_count(), 1u);
    const Node* n = g.find_node(id);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(n->hier_name, "mod.ff1");
    EXPECT_EQ(n->clock_domain, "clk");
    EXPECT_EQ(n->kind, NodeKind::Register);
}

TEST(GraphTest, AddPort) {
    Graph g;
    uint64_t id = g.add_port("mod.data_in", 8, {});
    EXPECT_EQ(g.register_count(), 0u);
    const Node* n = g.find_node(id);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(n->kind, NodeKind::Port);
    EXPECT_EQ(n->width, 8u);
}

TEST(GraphTest, AddEdge) {
    Graph g;
    uint64_t a = g.add_port("a", 1, {});
    uint64_t b = g.add_register("b", "clk", 1, {});
    g.add_edge(a, b);
    EXPECT_EQ(g.edge_count(), 1u);
}

TEST(GraphTest, SuccessorsAndPredecessors) {
    Graph g;
    uint64_t a = g.add_port("a", 1, {});
    uint64_t b = g.add_register("b", "clk", 1, {});
    uint64_t c = g.add_register("c", "clk", 1, {});
    g.add_edge(a, b);
    g.add_edge(b, c);

    auto succ = g.successors(a);
    ASSERT_EQ(succ.size(), 1u);
    EXPECT_EQ(succ[0], b);

    auto pred = g.predecessors(c);
    ASSERT_EQ(pred.size(), 1u);
    EXPECT_EQ(pred[0], b);
}

TEST(GraphTest, FindByName) {
    Graph g;
    g.add_register("mod.ff1", "clk", 1, {});
    const Node* n = g.find_node_by_name("mod.ff1");
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(n->hier_name, "mod.ff1");
    EXPECT_EQ(g.find_node_by_name("nonexistent"), nullptr);
}

TEST(GraphTest, MultipleDomains) {
    Graph g;
    uint64_t r1 = g.add_register("r1", "clk_a", 1, {});
    uint64_t r2 = g.add_register("r2", "clk_b", 1, {});
    g.add_edge(r1, r2);
    EXPECT_EQ(g.register_count(), 2u);
    EXPECT_EQ(g.find_node(r1)->clock_domain, "clk_a");
    EXPECT_EQ(g.find_node(r2)->clock_domain, "clk_b");
}
