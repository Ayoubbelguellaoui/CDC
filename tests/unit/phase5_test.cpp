#include <gtest/gtest.h>

#include <fstream>

#include "cdc/crossing.h"
#include "cdc/pattern.h"
#include "cdc/waiver.h"
#include "clock/domain.h"
#include "ir/graph.h"
#include "rules/rule.h"

using namespace opencdc::ir;
using namespace opencdc::cdc;
using namespace opencdc::clock;
using opencdc::rules::RuleEngine;

class Phase5Test : public ::testing::Test {
   protected:
    PatternRecognizer recognizer;
    Graph graph;
};

// === CDC rules exist ===

TEST_F(Phase5Test, RuleEngineHasCDC011) {
    RuleEngine engine;
    auto rule = engine.find_rule("CDC011");
    ASSERT_TRUE(rule.has_value());
    EXPECT_EQ(rule->name, "pulse_crossing");
    EXPECT_EQ(rule->severity, "warning");
}

TEST_F(Phase5Test, RuleEngineHasCDC012) {
    RuleEngine engine;
    auto rule = engine.find_rule("CDC012");
    ASSERT_TRUE(rule.has_value());
    EXPECT_EQ(rule->name, "toggle_crossing");
    EXPECT_EQ(rule->severity, "warning");
}

// === Pulse sync structural detection ===

TEST_F(Phase5Test, PulseSyncDetectedByStructure) {
    // Build: src_reg (clk_a) → XOR(src, src_delayed) → sync_stage (clk_b) → sync_stage2 (clk_b)
    uint64_t src = graph.add_register("top.src", "clk_a", 1, {"test.sv", 10, 5});
    uint64_t src_delayed = graph.add_register("top.src_d", "clk_a", 1, {"test.sv", 11, 5});
    uint64_t sync_stage = graph.add_register("top.sync", "clk_b", 1, {"test.sv", 12, 5});
    uint64_t sync_stage2 = graph.add_register("top.sync2", "clk_b", 1, {"test.sv", 13, 5});

    graph.add_edge(src, src_delayed);  // src feeds src_delayed (delay chain)
    std::vector<uint64_t> xor_inputs = {src, src_delayed};
    uint64_t xor_node =
        graph.add_combinational("top.edge_det", LogicType::Xor, xor_inputs, 1, {"test.sv", 12, 5});
    graph.add_edge(xor_node, sync_stage);
    graph.add_edge(sync_stage, sync_stage2);  // 2FF chain

    recognizer.ensure_patterns(graph);

    EXPECT_TRUE(recognizer.is_pulse_sync(src, graph));
    EXPECT_TRUE(recognizer.is_pulse_sync(sync_stage, graph));
    EXPECT_FALSE(recognizer.is_pulse_sync(src_delayed, graph));
}

// === Toggle sync structural detection ===

TEST_F(Phase5Test, ToggleSyncDetectedByStructure) {
    // Toggle register src (clk_a) feeds 2FF chain sync→sync2 (clk_b)
    // Also has XOR feedback: XOR output feeds back to src and to sync_stage
    uint64_t src = graph.add_register("top.src", "clk_a", 1, {"test.sv", 10, 5});
    uint64_t sync_stage = graph.add_register("top.sync", "clk_b", 1, {"test.sv", 11, 5});
    uint64_t sync_stage2 = graph.add_register("top.sync2", "clk_b", 1, {"test.sv", 12, 5});

    // Direct edge: register src → sync_stage (the XOR output feeds sync_stage)
    graph.add_edge(src, sync_stage);
    graph.add_edge(sync_stage, sync_stage2);

    // Toggle feedback: XOR of src with a different register, feeding back to src
    uint64_t const_reg = graph.add_register("top.one", "clk_a", 1, {"test.sv", 10, 6});
    std::vector<uint64_t> xor_inputs = {src, const_reg};
    uint64_t xor_node = graph.add_combinational("top.toggle_xor", LogicType::Xor, xor_inputs, 1,
                                                {"test.sv", 11, 5});
    graph.add_edge(xor_node, src);  // feedback: XOR → src

    recognizer.ensure_patterns(graph);

    EXPECT_TRUE(recognizer.is_toggle_sync(src, graph));
    EXPECT_TRUE(recognizer.is_toggle_sync(sync_stage, graph));
}

// === Waiver ticket field ===

TEST_F(Phase5Test, WaiverTicketParsed) {
    WaiverEngine engine;
    Waiver w;
    w.rule_id = "CDC001";
    w.source_reg_name = "top.src";
    w.dest_reg_name = "top.dst";
    w.justification = "Known safe";
    w.ticket = "TICKET-123";
    EXPECT_TRUE(engine.add_waiver(w));

    const auto& waivers = engine.waivers();
    ASSERT_EQ(waivers.size(), 1u);
    EXPECT_EQ(waivers[0].ticket, "TICKET-123");
}

TEST_F(Phase5Test, WaiverTicketTransferredToFinding) {
    WaiverEngine engine;
    Waiver w;
    w.rule_id = "CDC001";
    w.source_reg_name = "top.src";
    w.dest_reg_name = "top.dst";
    w.ticket = "JIRA-456";
    EXPECT_TRUE(engine.add_waiver(w));

    Finding f;
    f.rule_id = "CDC001";
    f.source_reg_name = "top.src";
    f.dest_reg_name = "top.dst";

    auto results = engine.apply({f});
    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results[0].waived);
    EXPECT_EQ(results[0].waiver_ticket, "JIRA-456");
}

TEST_F(Phase5Test, WaiverTicketInFileParsing) {
    // Create a temp waiver file with ticket
    std::string path = "/tmp/phase5_waiver_ticket.txt";
    {
        std::ofstream out(path);
        out << "CDC001 top.src top.dst clk_a clk_b \"test waiver\" @alice #TICKET-789\n";
    }

    WaiverEngine engine;
    std::string error;
    EXPECT_TRUE(engine.load_from_file(path, &error)) << error;
    ASSERT_EQ(engine.waivers().size(), 1u);
    EXPECT_EQ(engine.waivers()[0].ticket, "#TICKET-789");

    std::remove(path.c_str());
}

// === Over-broad waiver detection ===

TEST_F(Phase5Test, OverbroadWaiverDetected) {
    WaiverEngine engine;
    Waiver w;
    w.rule_id = "CDC001";
    w.source_reg_name = "";
    w.dest_reg_name = "";
    EXPECT_TRUE(engine.add_waiver(w));

    std::vector<Finding> findings;
    for (int i = 0; i < 15; ++i) {
        Finding f;
        f.rule_id = "CDC001";
        f.source_reg_name = "top.src" + std::to_string(i);
        f.dest_reg_name = "top.dst" + std::to_string(i);
        findings.push_back(f);
    }

    auto warnings = engine.check_unused(findings);
    bool found = false;
    for (const auto& w : warnings) {
        if (w.find("overly broad") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(Phase5Test, NonOverbroadWaiverNotFlagged) {
    WaiverEngine engine;
    Waiver w;
    w.rule_id = "CDC001";
    w.source_reg_name = "top.src";
    w.dest_reg_name = "top.dst";
    EXPECT_TRUE(engine.add_waiver(w));

    std::vector<Finding> findings;
    for (int i = 0; i < 5; ++i) {
        Finding f;
        f.rule_id = "CDC001";
        f.source_reg_name = "top.src";
        f.dest_reg_name = "top.dst";
        findings.push_back(f);
    }

    auto warnings = engine.check_unused(findings);
    for (const auto& w : warnings) {
        EXPECT_TRUE(w.find("overly broad") == std::string::npos);
    }
}
