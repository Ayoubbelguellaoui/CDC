#include "cdc/pattern.h"
#include "ir/graph.h"
#include <gtest/gtest.h>

using namespace opencdc::ir;
using namespace opencdc::cdc;

class PatternRecognizerTest : public ::testing::Test {
protected:
    PatternRecognizer recognizer;
    Graph graph;
};

TEST_F(PatternRecognizerTest, DetectsGrayEncoder) {
    uint64_t reg = graph.add_register("top.enc", "clk_a", 8, {"test.sv", 10, 5});
    auto* node = graph.find_node_mutable(reg);
    node->logic_type = LogicType::GrayEncoder;

    EXPECT_TRUE(recognizer.is_gray_coded(reg, graph));
}

TEST_F(PatternRecognizerTest, DetectsGrayDecoder) {
    uint64_t reg = graph.add_register("top.dec", "clk_a", 8, {"test.sv", 10, 5});
    auto* node = graph.find_node_mutable(reg);
    node->logic_type = LogicType::GrayDecoder;

    EXPECT_TRUE(recognizer.is_gray_coded(reg, graph));
}

TEST_F(PatternRecognizerTest, DetectsGrayByFlag) {
    uint64_t reg = graph.add_register("top.counter", "clk_a", 8, {"test.sv", 10, 5});
    auto* node = graph.find_node_mutable(reg);
    node->is_gray_coded = true;

    EXPECT_TRUE(recognizer.is_gray_coded(reg, graph));
}

TEST_F(PatternRecognizerTest, DetectsHandshakeValid) {
    uint64_t reg = graph.add_register("top.data_valid", "clk_a", 1, {"test.sv", 10, 5});
    auto* node = graph.find_node_mutable(reg);
    node->logic_type = LogicType::HandshakeValid;

    EXPECT_TRUE(recognizer.is_handshake_signal(reg, graph));
}

TEST_F(PatternRecognizerTest, DetectsHandshakeReady) {
    uint64_t reg = graph.add_register("top.data_ready", "clk_a", 1, {"test.sv", 10, 5});
    auto* node = graph.find_node_mutable(reg);
    node->logic_type = LogicType::HandshakeReady;

    EXPECT_TRUE(recognizer.is_handshake_signal(reg, graph));
}

TEST_F(PatternRecognizerTest, DetectsHandshakeByFlag) {
    uint64_t reg = graph.add_register("top.req", "clk_a", 1, {"test.sv", 10, 5});
    auto* node = graph.find_node_mutable(reg);
    node->is_handshake_signal = true;

    EXPECT_TRUE(recognizer.is_handshake_signal(reg, graph));
}

TEST_F(PatternRecognizerTest, DetectsAsyncFifoPtr) {
    uint64_t reg = graph.add_register("top.ptr", "clk_a", 4, {"test.sv", 10, 5});
    auto* node = graph.find_node_mutable(reg);
    node->logic_type = LogicType::AsyncFifoPtr;

    EXPECT_TRUE(recognizer.is_async_fifo_ptr(reg, graph));
}

TEST_F(PatternRecognizerTest, DetectsAsyncFifoPtrByFlag) {
    uint64_t reg = graph.add_register("top.wptr", "clk_b", 4, {"test.sv", 10, 5});
    auto* node = graph.find_node_mutable(reg);
    node->is_async_fifo_ptr = true;

    EXPECT_TRUE(recognizer.is_async_fifo_ptr(reg, graph));
}

TEST_F(PatternRecognizerTest, NoFalsePositiveForRegularRegister) {
    uint64_t reg = graph.add_register("top.counter", "clk_a", 8, {"test.sv", 10, 5});

    EXPECT_FALSE(recognizer.is_gray_coded(reg, graph));
    EXPECT_FALSE(recognizer.is_handshake_signal(reg, graph));
    EXPECT_FALSE(recognizer.is_async_fifo_ptr(reg, graph));
}

TEST_F(PatternRecognizerTest, AnnotatesGraph) {
    uint64_t gray_reg = graph.add_register("top.ptr", "clk_a", 8, {"test.sv", 10, 5});
    uint64_t valid_reg = graph.add_register("top.req", "clk_a", 1, {"test.sv", 11, 5});
    uint64_t normal_reg = graph.add_register("top.counter", "clk_a", 8, {"test.sv", 12, 5});

    auto* gray_node = graph.find_node_mutable(gray_reg);
    gray_node->logic_type = LogicType::GrayEncoder;
    auto* valid_node = graph.find_node_mutable(valid_reg);
    valid_node->logic_type = LogicType::HandshakeValid;

    recognizer.analyze_and_annotate(graph);

    gray_node = graph.find_node_mutable(gray_reg);
    ASSERT_NE(gray_node, nullptr);
    EXPECT_TRUE(gray_node->is_gray_coded);

    valid_node = graph.find_node_mutable(valid_reg);
    ASSERT_NE(valid_node, nullptr);
    EXPECT_TRUE(valid_node->is_handshake_signal);

    Node* normal_node = graph.find_node_mutable(normal_reg);
    ASSERT_NE(normal_node, nullptr);
    EXPECT_FALSE(normal_node->is_gray_coded);
    EXPECT_FALSE(normal_node->is_handshake_signal);
}

TEST_F(PatternRecognizerTest, DetectsAsyncFifoPattern) {
    uint64_t rd_ptr = graph.add_register("async_inst.rd_ptr", "clk_rd", 4, {"test.sv", 10, 5});
    uint64_t wr_ptr = graph.add_register("async_inst.wr_ptr", "clk_wr", 4, {"test.sv", 11, 5});
    graph.add_register("async_inst.async_fifo_buffer", "clk_rd", 4, {"test.sv", 12, 5});

    auto* rd_node = graph.find_node_mutable(rd_ptr);
    rd_node->logic_type = LogicType::AsyncFifoPtr;
    rd_node->is_gray_coded = true;
    auto* wr_node = graph.find_node_mutable(wr_ptr);
    wr_node->logic_type = LogicType::AsyncFifoPtr;
    wr_node->is_gray_coded = true;

    graph.add_edge(wr_ptr, rd_ptr);

    auto fifos = recognizer.detect_async_fifos(graph);

    EXPECT_EQ(fifos.size(), 1u);
    EXPECT_EQ(fifos[0].read_ptr_id, rd_ptr);
    EXPECT_EQ(fifos[0].write_ptr_id, wr_ptr);
}

TEST_F(PatternRecognizerTest, DetectsHandshakePattern) {
    uint64_t valid = graph.add_register("top.req", "clk_a", 1, {"test.sv", 10, 5});
    uint64_t ready = graph.add_register("top.ack", "clk_b", 1, {"test.sv", 11, 5});

    auto* valid_node = graph.find_node_mutable(valid);
    valid_node->logic_type = LogicType::HandshakeValid;
    auto* ready_node = graph.find_node_mutable(ready);
    ready_node->logic_type = LogicType::HandshakeReady;

    auto handshakes = recognizer.detect_handshakes(graph);

    EXPECT_EQ(handshakes.size(), 1u);
    EXPECT_EQ(handshakes[0].valid_id, valid);
    EXPECT_EQ(handshakes[0].ready_id, ready);
}

TEST_F(PatternRecognizerTest, DetectsGrayEncodingPattern) {
    uint64_t encoder = graph.add_register("top.enc", "clk_a", 8, {"test.sv", 10, 5});
    uint64_t decoder = graph.add_register("top.dec", "clk_b", 8, {"test.sv", 11, 5});

    auto* enc_node = graph.find_node_mutable(encoder);
    enc_node->logic_type = LogicType::GrayEncoder;
    auto* dec_node = graph.find_node_mutable(decoder);
    dec_node->logic_type = LogicType::GrayDecoder;

    graph.add_edge(encoder, decoder);

    auto patterns = recognizer.detect_gray_encoding(graph);

    EXPECT_EQ(patterns.size(), 1u);
    EXPECT_EQ(patterns[0].encoder_id, encoder);
    EXPECT_EQ(patterns[0].decoder_id, decoder);
}

TEST_F(PatternRecognizerTest, MultiplePatternsOnSameNode) {
    uint64_t reg = graph.add_register("top.ptr", "clk_a", 4, {"test.sv", 10, 5});
    auto* node = graph.find_node_mutable(reg);
    node->is_gray_coded = true;
    node->is_handshake_signal = true;

    EXPECT_TRUE(recognizer.is_gray_coded(reg, graph));
    EXPECT_TRUE(recognizer.is_handshake_signal(reg, graph));
}

TEST_F(PatternRecognizerTest, DetectsStructuralGrayEncoderOnCombNode) {
    // bin register + delayed copy feeding an XOR comb node = gray encoder.
    uint64_t bin = graph.add_register("top.bin", "clk_a", 8, {"test.sv", 10, 5});
    uint64_t bin_delay = graph.add_register("top.bin_d", "clk_a", 8, {"test.sv", 11, 5});
    uint64_t gray_reg = graph.add_register("top.gray", "clk_a", 8, {"test.sv", 12, 5});

    // bin_delay is a one-cycle delay of bin: x ^ (x >> 1) structure.
    graph.add_edge(bin, bin_delay);
    std::vector<uint64_t> xor_inputs = {bin, bin_delay};
    uint64_t comb = graph.add_combinational("top.gray$comb", LogicType::Xor,
                                            xor_inputs, 8, {"test.sv", 12, 5});
    graph.add_edge(comb, gray_reg);

    auto patterns = recognizer.detect_gray_encoding(graph);

    ASSERT_EQ(patterns.size(), 1u);
    EXPECT_EQ(patterns[0].encoder_id, comb);
    EXPECT_EQ(patterns[0].decoder_id, 0u);
    EXPECT_TRUE(patterns[0].verified);
}

TEST_F(PatternRecognizerTest, FlaggedGraySourceIsSafeCrossingSource) {
    // Frontend sets is_gray_coded on a gray-transform register (e.g.
    // gray <= bin ^ (bin >> 1)). Crossing from that register is safe even
    // though the IR has no decoder node (decode happens downstream).
    uint64_t src = graph.add_register("top.gray_src", "clk_a", 8, {"test.sv", 10, 5});
    uint64_t dst = graph.add_register("top.dst", "clk_b", 8, {"test.sv", 11, 5});
    graph.find_node_mutable(src)->is_gray_coded = true;
    graph.add_edge(src, dst);

    EXPECT_TRUE(recognizer.is_verified_safe_crossing(src, dst, graph));
}

TEST_F(PatternRecognizerTest, UnrelatedDestNotSafeForEncoderPair) {
    // Encoder→decoder pair is safe only toward the paired decoder, not an
    // arbitrary destination that merely shares the graph.
    uint64_t encoder = graph.add_register("top.enc", "clk_a", 8, {"test.sv", 10, 5});
    uint64_t decoder = graph.add_register("top.dec", "clk_b", 8, {"test.sv", 11, 5});
    uint64_t other = graph.add_register("top.other", "clk_b", 8, {"test.sv", 12, 5});

    graph.find_node_mutable(encoder)->logic_type = LogicType::GrayEncoder;
    graph.find_node_mutable(decoder)->logic_type = LogicType::GrayDecoder;
    graph.add_edge(encoder, decoder);
    graph.add_edge(encoder, other);

    EXPECT_TRUE(recognizer.is_verified_safe_crossing(encoder, decoder, graph));
    EXPECT_FALSE(recognizer.is_verified_safe_crossing(encoder, other, graph));
}

TEST_F(PatternRecognizerTest, PlainMultiBitSourceNeverSafe) {
    // No pattern evidence at all: crossing must not be considered safe.
    uint64_t src = graph.add_register("top.bus", "clk_a", 8, {"test.sv", 10, 5});
    uint64_t dst = graph.add_register("top.dst", "clk_b", 8, {"test.sv", 11, 5});
    graph.add_edge(src, dst);

    EXPECT_FALSE(recognizer.is_verified_safe_crossing(src, dst, graph));
}

TEST_F(PatternRecognizerTest, VerifiedFifoPairSuppressesCrossing) {
    uint64_t rd_ptr = graph.add_register("fifo.rd_ptr", "clk_rd", 4, {"test.sv", 10, 5});
    uint64_t wr_ptr = graph.add_register("fifo.wr_ptr", "clk_wr", 4, {"test.sv", 11, 5});
    graph.find_node_mutable(rd_ptr)->is_async_fifo_ptr = true;
    graph.find_node_mutable(rd_ptr)->is_gray_coded = true;
    graph.find_node_mutable(wr_ptr)->is_async_fifo_ptr = true;
    graph.find_node_mutable(wr_ptr)->is_gray_coded = true;
    graph.add_edge(wr_ptr, rd_ptr);

    EXPECT_TRUE(recognizer.is_verified_safe_crossing(wr_ptr, rd_ptr, graph));
    // Un-gray-coded FIFO pointers are detected but not verified.
    uint64_t rd2 = graph.add_register("fifo2.rd_ptr", "clk_rd", 4, {"test.sv", 20, 5});
    uint64_t wr2 = graph.add_register("fifo2.wr_ptr", "clk_wr", 4, {"test.sv", 21, 5});
    graph.find_node_mutable(rd2)->is_async_fifo_ptr = true;
    graph.find_node_mutable(wr2)->is_async_fifo_ptr = true;
    graph.add_edge(wr2, rd2);

    EXPECT_FALSE(recognizer.is_verified_safe_crossing(wr2, rd2, graph));
}

TEST_F(PatternRecognizerTest, AnnotatePropagatesDetectedPatterns) {
    // analyze_and_annotate must write detector results back onto node flags.
    uint64_t rd_ptr = graph.add_register("fifo.rd_ptr", "clk_rd", 4, {"test.sv", 10, 5});
    uint64_t wr_ptr = graph.add_register("fifo.wr_ptr", "clk_wr", 4, {"test.sv", 11, 5});
    graph.find_node_mutable(rd_ptr)->is_async_fifo_ptr = true;
    graph.find_node_mutable(rd_ptr)->is_gray_coded = true;
    graph.find_node_mutable(wr_ptr)->is_async_fifo_ptr = true;
    graph.find_node_mutable(wr_ptr)->is_gray_coded = true;
    graph.add_edge(wr_ptr, rd_ptr);

    recognizer.analyze_and_annotate(graph);

    // Flags remain set after annotation (not clobbered).
    EXPECT_TRUE(graph.find_node_mutable(rd_ptr)->is_async_fifo_ptr);
    EXPECT_TRUE(graph.find_node_mutable(rd_ptr)->is_gray_coded);
}
