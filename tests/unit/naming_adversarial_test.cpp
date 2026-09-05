#include "cdc/pattern.h"
#include "cdc/crossing.h"
#include "ir/graph.h"
#include "clock/domain.h"
#include <gtest/gtest.h>

using namespace opencdc::ir;
using namespace opencdc::cdc;
using namespace opencdc::clock;

class NamingAdversarialTest : public ::testing::Test {
protected:
    Graph graph;
    PatternRecognizer recognizer;
};

TEST_F(NamingAdversarialTest, GrayByNameDoesNotBypassCdc002) {
    uint64_t src = graph.add_register("mod.gray_data", "clk_a", 8, {"mod.sv", 5, 5});
    uint64_t dst = graph.add_register("mod.dst_ff", "clk_b", 8, {"mod.sv", 6, 5});
    auto* s = graph.find_node_mutable(src);
    s->reset_signal = "rst_n";
    auto* d = graph.find_node_mutable(dst);
    d->reset_signal = "rst_n";
    graph.add_edge(src, dst);

    recognizer.analyze_and_annotate(graph);

    EXPECT_FALSE(recognizer.is_gray_coded(src, graph))
        << "Register named 'gray_data' without gray encoding structure should NOT be gray-coded";
}

TEST_F(NamingAdversarialTest, HandshakeByNameDoesNotBypassCdc002) {
    uint64_t src = graph.add_register("mod.valid_reg", "clk_a", 8, {"mod.sv", 5, 5});
    uint64_t dst = graph.add_register("mod.dst_ff", "clk_b", 8, {"mod.sv", 6, 5});
    auto* s = graph.find_node_mutable(src);
    s->reset_signal = "rst_n";
    auto* d = graph.find_node_mutable(dst);
    d->reset_signal = "rst_n";
    graph.add_edge(src, dst);

    recognizer.analyze_and_annotate(graph);

    EXPECT_FALSE(recognizer.is_handshake_signal(src, graph))
        << "Register named 'valid_reg' without handshake structure should NOT be handshake";
}

TEST_F(NamingAdversarialTest, FifoPtrByNameDoesNotBypassSafeCheck) {
    uint64_t src = graph.add_register("mod.wr_ptr", "clk_a", 8, {"mod.sv", 5, 5});
    uint64_t dst = graph.add_register("mod.rd_ptr", "clk_b", 8, {"mod.sv", 6, 5});
    auto* s = graph.find_node_mutable(src);
    s->reset_signal = "rst_n";
    auto* d = graph.find_node_mutable(dst);
    d->reset_signal = "rst_n";
    graph.add_edge(src, dst);

    recognizer.analyze_and_annotate(graph);

    EXPECT_FALSE(recognizer.is_verified_safe_crossing(src, dst, graph))
        << "Registers named 'wr_ptr'/'rd_ptr' without FIFO structure should NOT be verified safe";
}

TEST_F(NamingAdversarialTest, SyncByNameDoesNotBypassSyncDetection) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t meta = graph.add_register("mod.sync_meta", "clk_a", 1, {"mod.sv", 8, 5});

    graph.add_edge(src, meta);

    recognizer.analyze_and_annotate(graph);

    SynchronizerMatcher matcher;
    auto pat = matcher.find_pattern_for_dest(meta, graph);
    EXPECT_EQ(pat, SyncPattern::None)
        << "Register named 'sync_meta' without cross-domain source should NOT be sync pattern";
}

TEST_F(NamingAdversarialTest, FrontendNameTaggingDoesNotBypassSafety) {
    uint64_t src = graph.add_register("mod.foo_reg", "clk_a", 8, {"mod.sv", 5, 5});
    uint64_t dst = graph.add_register("mod.bar_reg", "clk_b", 8, {"mod.sv", 6, 5});
    auto* s = graph.find_node_mutable(src);
    s->is_handshake_signal = true;
    s->reset_signal = "rst_n";
    auto* d = graph.find_node_mutable(dst);
    d->is_handshake_signal = true;
    d->reset_signal = "rst_n";
    graph.add_edge(src, dst);

    recognizer.analyze_and_annotate(graph);

    EXPECT_FALSE(recognizer.is_verified_safe_crossing(src, dst, graph))
        << "Frontend name tagging (is_handshake_signal) should not bypass "
           "structural verification of actual handshake protocol topology";
}

TEST_F(NamingAdversarialTest, HandshakeWithoutFeedbackPathNotSafe) {
    // Two cross-domain registers with handshake LogicTypes but NO feedback path
    // (ready has no register successor back in the source domain).
    // The feedback path check should prevent this from being verified safe.
    uint64_t valid = graph.add_register("top.valid_reg", "clk_a", 8, {"test.sv", 10, 5});
    uint64_t ready = graph.add_register("top.ready_reg", "clk_b", 8, {"test.sv", 11, 5});
    graph.find_node_mutable(valid)->logic_type = LogicType::HandshakeValid;
    graph.find_node_mutable(ready)->logic_type = LogicType::HandshakeReady;
    graph.add_edge(valid, ready);

    recognizer.analyze_and_annotate(graph);

    EXPECT_FALSE(recognizer.is_verified_safe_crossing(valid, ready, graph))
        << "Handshake without feedback path should NOT be verified safe";
}

TEST_F(NamingAdversarialTest, HandshakeWithFeedbackPathIsSafe) {
    // Two cross-domain registers with handshake LogicTypes AND a feedback path:
    // ready → ... → register in source domain (ack back).
    uint64_t valid = graph.add_register("top.valid_reg", "clk_a", 8, {"test.sv", 10, 5});
    uint64_t ready = graph.add_register("top.ready_reg", "clk_b", 8, {"test.sv", 11, 5});
    uint64_t ack = graph.add_register("top.ack_reg", "clk_a", 8, {"test.sv", 12, 5});
    graph.find_node_mutable(valid)->logic_type = LogicType::HandshakeValid;
    graph.find_node_mutable(ready)->logic_type = LogicType::HandshakeReady;
    graph.add_edge(valid, ready);
    graph.add_edge(ready, ack);

    recognizer.analyze_and_annotate(graph);

    EXPECT_TRUE(recognizer.is_verified_safe_crossing(valid, ready, graph))
        << "Handshake with feedback path should be verified safe";
}
