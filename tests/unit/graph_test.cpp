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

TEST(GraphTest, AddNet) {
    Graph g;
    uint64_t id = g.add_net("mod.wire1", 4, {});
    const Node* n = g.find_node(id);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(n->kind, NodeKind::Net);
    EXPECT_EQ(n->width, 4u);
}

TEST(GraphTest, RegisterPredecessorsThroughNet) {
    Graph g;
    uint64_t src = g.add_register("src", "clk_a", 1, {});
    uint64_t wire = g.add_net("wire", 1, {});
    uint64_t dst = g.add_register("dst", "clk_b", 1, {});
    g.add_edge(src, wire);
    g.add_edge(wire, dst);

    auto reg_preds = g.register_predecessors(dst);
    ASSERT_EQ(reg_preds.size(), 1u);
    EXPECT_EQ(reg_preds[0], src);
}

TEST(GraphTest, RegisterSuccessorsThroughNet) {
    Graph g;
    uint64_t src = g.add_register("src", "clk_a", 1, {});
    uint64_t wire = g.add_net("wire", 1, {});
    uint64_t dst = g.add_register("dst", "clk_b", 1, {});
    g.add_edge(src, wire);
    g.add_edge(wire, dst);

    auto reg_succs = g.register_successors(src);
    ASSERT_EQ(reg_succs.size(), 1u);
    EXPECT_EQ(reg_succs[0], dst);
}

TEST(GraphTest, RegisterSuccessorsThroughCombinational) {
    Graph g;
    uint64_t src = g.add_register("src", "clk_a", 8, {});
    uint64_t comb = g.add_combinational("comb", LogicType::Xor, {src}, 8, {});
    uint64_t dst = g.add_register("dst", "clk_b", 8, {});
    g.add_edge(comb, dst);

    auto reg_succs = g.register_successors(src);
    ASSERT_EQ(reg_succs.size(), 1u);
    EXPECT_EQ(reg_succs[0], dst);
}

TEST(GraphTest, FindRegisterPaths) {
    Graph g;
    uint64_t src = g.add_register("src", "clk_a", 1, {});
    uint64_t wire = g.add_net("wire", 1, {});
    uint64_t dst = g.add_register("dst", "clk_b", 1, {});
    g.add_edge(src, wire);
    g.add_edge(wire, dst);

    auto result = g.find_register_paths(src);
    ASSERT_EQ(result.paths.size(), 1u);
    EXPECT_EQ(result.paths[0].src_reg_id, src);
    EXPECT_EQ(result.paths[0].dst_reg_id, dst);
    EXPECT_EQ(result.paths[0].node_ids.size(), 3u);
}

TEST(GraphTest, FindRegisterPathsMultiple) {
    Graph g;
    uint64_t src = g.add_register("src", "clk_a", 1, {});
    uint64_t w1 = g.add_net("w1", 1, {});
    uint64_t w2 = g.add_net("w2", 1, {});
    uint64_t dst1 = g.add_register("dst1", "clk_b", 1, {});
    uint64_t dst2 = g.add_register("dst2", "clk_b", 1, {});
    g.add_edge(src, w1);
    g.add_edge(src, w2);
    g.add_edge(w1, dst1);
    g.add_edge(w2, dst2);

    auto result = g.find_register_paths(src);
    EXPECT_EQ(result.paths.size(), 2u);
}

TEST(GraphTest, ValidateAcceptsWellFormedGraph) {
    Graph g;
    uint64_t src = g.add_register("src", "clk_a", 1, {});
    uint64_t dst = g.add_register("dst", "clk_b", 1, {});
    g.add_edge(src, dst);

    EXPECT_TRUE(g.validate().ok());
}

TEST(GraphTest, RegisterPathLimitBoundsFanout) {
    Graph g;
    uint64_t src = g.add_register("src", "clk_a", 1, {});
    for (int i = 0; i < 20; ++i) {
        uint64_t wire = g.add_net("wire" + std::to_string(i), 1, {});
        uint64_t dst = g.add_register("dst" + std::to_string(i), "clk_b", 1, {});
        g.add_edge(src, wire);
        g.add_edge(wire, dst);
    }

    auto result = g.find_register_paths(src, 5);
    EXPECT_TRUE(result.truncated);
    EXPECT_EQ(result.paths.size(), 5u);
}
