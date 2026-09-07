#include <gtest/gtest.h>

#include "analysis/analyzer.h"
#include "cdc/crossing.h"
#include "clock/domain.h"
#include "ir/graph.h"

struct ExpectedFinding {
    std::string rule_id;
    std::string severity;
    std::string source_domain;
    std::string dest_domain;
};

class GoldenCorpusTest : public ::testing::Test {};

TEST_F(GoldenCorpusTest, SameDomainNoFindings) {
    opencdc::ir::Graph graph;
    uint64_t a = graph.add_register("mod.ff_a", "clk_a", 1, {"same_domain.sv", 5, 5});
    uint64_t b = graph.add_register("mod.ff_b", "clk_a", 1, {"same_domain.sv", 6, 5});
    graph.add_edge(a, b);

    opencdc::clock::DomainExtractor de;
    auto dr = de.extract(graph);

    opencdc::cdc::CrossingAnalyzer ca;
    auto findings = ca.analyze(graph, dr.domains, dr.register_to_domain);

    // Same domain: no crossing findings expected.
    bool has_cdc001 = false;
    for (const auto& f : findings) {
        if (f.rule_id == "CDC001")
            has_cdc001 = true;
    }
    EXPECT_FALSE(has_cdc001);
}

TEST_F(GoldenCorpusTest, SimpleCrossingDetected) {
    opencdc::ir::Graph graph;
    uint64_t a = graph.add_register("mod.src_ff", "clk_a", 1, {"simple_cross.sv", 5, 5});
    uint64_t b = graph.add_register("mod.dst_ff", "clk_b", 1, {"simple_cross.sv", 6, 5});
    auto* na = graph.find_node_mutable(a);
    na->reset_signal = "rst_n";
    auto* nb = graph.find_node_mutable(b);
    nb->reset_signal = "rst_n";
    graph.add_edge(a, b);

    opencdc::clock::DomainExtractor de;
    auto dr = de.extract(graph);

    opencdc::cdc::CrossingAnalyzer ca;
    auto findings = ca.analyze(graph, dr.domains, dr.register_to_domain);

    bool found_cdc001_error = false;
    for (const auto& f : findings) {
        if (f.rule_id == "CDC001" && f.severity == "error") {
            found_cdc001_error = true;
            EXPECT_EQ(f.source_domain, "clk_a");
            EXPECT_EQ(f.dest_domain, "clk_b");
        }
    }
    EXPECT_TRUE(found_cdc001_error);
}

TEST_F(GoldenCorpusTest, SynchronizedCrossingWarning) {
    opencdc::ir::Graph graph;
    uint64_t src = graph.add_register("mod.src_ff", "clk_a", 1, {"sync_cross.sv", 5, 5});
    uint64_t sync1 = graph.add_register("mod.sync_ff_1", "clk_b", 1, {"sync_cross.sv", 10, 5});
    uint64_t sync2 = graph.add_register("mod.sync_ff_2", "clk_b", 1, {"sync_cross.sv", 11, 5});
    uint64_t dst = graph.add_register("mod.dst_ff", "clk_b", 1, {"sync_cross.sv", 15, 5});
    auto* ns = graph.find_node_mutable(src);
    ns->reset_signal = "rst_n";
    auto* nd = graph.find_node_mutable(dst);
    nd->reset_signal = "rst_n";

    graph.add_edge(src, sync1);
    graph.add_edge(sync1, sync2);
    graph.add_edge(sync2, dst);

    opencdc::clock::DomainExtractor de;
    auto dr = de.extract(graph);

    opencdc::cdc::PatternRecognizer pr;
    pr.analyze_and_annotate(graph);

    opencdc::cdc::CrossingAnalyzer ca;
    ca.set_pattern_recognizer(&pr);
    auto findings = ca.analyze(graph, dr.domains, dr.register_to_domain);

    // With 2FF sync chain, CDC001 should be warning (not error).
    bool found_cdc001 = false;
    for (const auto& f : findings) {
        if (f.rule_id == "CDC001") {
            found_cdc001 = true;
            EXPECT_EQ(f.severity, "warning");
        }
    }
    EXPECT_TRUE(found_cdc001);
}

TEST_F(GoldenCorpusTest, MultiBitCrossingDetected) {
    opencdc::ir::Graph graph;
    uint64_t src = graph.add_register("mod.src_bus", "clk_a", 8, {"multi_bit.sv", 5, 5});
    uint64_t dst = graph.add_register("mod.dst_bus", "clk_b", 8, {"multi_bit.sv", 6, 5});
    auto* ns = graph.find_node_mutable(src);
    ns->reset_signal = "rst_n";
    auto* nd = graph.find_node_mutable(dst);
    nd->reset_signal = "rst_n";
    graph.add_edge(src, dst);

    opencdc::clock::DomainExtractor de;
    auto dr = de.extract(graph);

    opencdc::cdc::CrossingAnalyzer ca;
    auto findings = ca.analyze(graph, dr.domains, dr.register_to_domain);

    bool found_cdc001 = false, found_cdc002 = false;
    for (const auto& f : findings) {
        if (f.rule_id == "CDC001" && f.severity == "error")
            found_cdc001 = true;
        if (f.rule_id == "CDC002" && f.severity == "error")
            found_cdc002 = true;
    }
    EXPECT_TRUE(found_cdc001);
    EXPECT_TRUE(found_cdc002);
}

TEST_F(GoldenCorpusTest, BlackboxCrossingSuppressed) {
    opencdc::ir::Graph graph;
    uint64_t src = graph.add_register("mod.src_ff", "clk_a", 1, {"bb_cross.sv", 5, 5}, "mod");
    uint64_t mid = graph.add_register("mod.xpm_cdc_gray_inst/reg", "clk_b", 1,
                                      {"bb_cross.sv", 10, 5}, "mod.xpm_cdc_gray");
    uint64_t dst = graph.add_register("mod.dst_ff", "clk_b", 1, {"bb_cross.sv", 15, 5}, "mod");
    auto* ns = graph.find_node_mutable(src);
    ns->reset_signal = "rst_n";
    auto* nd = graph.find_node_mutable(dst);
    nd->reset_signal = "rst_n";

    graph.add_edge(src, mid);
    graph.add_edge(mid, dst);

    opencdc::clock::DomainExtractor de;
    auto dr = de.extract(graph);

    opencdc::cdc::BlackBoxRegistry registry;
    opencdc::cdc::CrossingAnalyzer ca;
    ca.set_blackbox_registry(&registry);
    auto findings = ca.analyze(graph, dr.domains, dr.register_to_domain);

    bool found_bb_suppressed = false;
    for (const auto& f : findings) {
        if (f.safety_status == opencdc::cdc::SafetyStatus::VerifiedSafe &&
            f.safety_provenance.find("black box") != std::string::npos) {
            found_bb_suppressed = true;
            EXPECT_EQ(f.severity, "info");
        }
    }
    EXPECT_TRUE(found_bb_suppressed);
}

TEST_F(GoldenCorpusTest, ExclusiveClockGroupSuppressed) {
    opencdc::ir::Graph graph;
    uint64_t src = graph.add_register("mod.src_ff", "clk_a", 1, {"excl.sv", 5, 5});
    uint64_t dst = graph.add_register("mod.dst_ff", "clk_b", 1, {"excl.sv", 6, 5});
    auto* ns = graph.find_node_mutable(src);
    ns->reset_signal = "rst_n";
    auto* nd = graph.find_node_mutable(dst);
    nd->reset_signal = "rst_n";
    graph.add_edge(src, dst);

    opencdc::clock::DomainExtractor de;
    auto dr = de.extract(graph);

    opencdc::clock::ClockConstraints cc;
    cc.false_paths.push_back({"clk_a", "clk_b", "", "", {}, "", "", "", "", ""});

    opencdc::cdc::CrossingAnalyzer ca;
    ca.set_clock_constraints(&cc);
    auto findings = ca.analyze(graph, dr.domains, dr.register_to_domain);

    // Exclusive/false path crossing: should be info, not error.
    bool found_info = false;
    for (const auto& f : findings) {
        if (f.rule_id == "CDC001" && f.severity == "info") {
            found_info = true;
            EXPECT_TRUE(f.suppressed_by_false_path);
        }
    }
    EXPECT_TRUE(found_info);
}

TEST_F(GoldenCorpusTest, CoverageAndSignoffBasic) {
    opencdc::ir::Graph graph;
    uint64_t src = graph.add_register("mod.src_ff", "clk_a", 1, {"cov.sv", 5, 5});
    uint64_t dst = graph.add_register("mod.dst_ff", "clk_b", 1, {"cov.sv", 6, 5});
    auto* ns = graph.find_node_mutable(src);
    ns->reset_signal = "rst_n";
    auto* nd = graph.find_node_mutable(dst);
    nd->reset_signal = "rst_n";
    graph.add_edge(src, dst);

    opencdc::clock::DomainExtractor de;
    auto dr = de.extract(graph);

    opencdc::cdc::CrossingAnalyzer ca;
    auto findings = ca.analyze(graph, dr.domains, dr.register_to_domain);

    opencdc::analysis::CoverageEngine ce;
    auto coverage = ce.compute(findings, "complete");

    EXPECT_GT(coverage.counts.total, 0u);

    opencdc::analysis::SignoffEngine se;
    auto signoff = se.evaluate(findings, "complete");

    EXPECT_EQ(signoff.status, opencdc::analysis::SignoffStatus::Fail);
    EXPECT_GT(signoff.coverage.counts.unwaived_errors, 0u);
}

TEST_F(GoldenCorpusTest, EmptyGraphNoFindings) {
    opencdc::ir::Graph graph;
    opencdc::clock::DomainExtractor de;
    auto dr = de.extract(graph);

    opencdc::cdc::CrossingAnalyzer ca;
    auto findings = ca.analyze(graph, dr.domains, dr.register_to_domain);

    EXPECT_TRUE(findings.empty());

    opencdc::analysis::CoverageEngine ce;
    auto coverage = ce.compute(findings, "complete");
    EXPECT_EQ(coverage.counts.total, 0u);

    opencdc::analysis::SignoffEngine se;
    auto signoff = se.evaluate(findings, "complete");
    EXPECT_EQ(signoff.status, opencdc::analysis::SignoffStatus::Pass);
}
