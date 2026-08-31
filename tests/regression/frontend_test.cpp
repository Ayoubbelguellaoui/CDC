#include <gtest/gtest.h>
#include "frontend/slang_adapter.h"
#include "clock/domain.h"
#include "cdc/crossing.h"
#include "cdc/reconvergence.h"
#include "cdc/waiver.h"
#include "rules/rule.h"
#include "report/report.h"
#include <gtest/gtest.h>
#include <string>

using opencdc::ir::NodeKind;
using opencdc::ir::ResetPolarity;
namespace cdc_clock = opencdc::clock;
namespace cdc_ns = opencdc::cdc;

static std::string fixture_path(const std::string& name) {
    return std::string(FIXTURES_DIR) + "/sv/" + name;
}

TEST(FrontendTest, CdcCrossingHasTwoRegisters) {
    opencdc::frontend::SlangAdapter adapter;
    auto result = adapter.elaborate(
        {fixture_path("cdc_crossing.sv")}, "simple_cdc_crossing");
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.graph.register_count(), 2u);
    EXPECT_GE(result.graph.edge_count(), 1u);
}

TEST(FrontendTest, AsyncResetEventListRolesClassifiedCorrectly) {
    // `@(posedge clk or negedge rst_n)`: the clock is the last non-reset
    // event and rst_n is the async reset — regardless of event order.
    opencdc::frontend::SlangAdapter adapter;
    auto result = adapter.elaborate(
        {fixture_path("async_reset_2ff.sv")}, "async_reset_2ff");
    ASSERT_TRUE(result.ok);

    const auto* meta = result.graph.find_node_by_name("async_reset_2ff.meta");
    ASSERT_NE(meta, nullptr);
    EXPECT_EQ(meta->kind, NodeKind::Register);
    EXPECT_EQ(meta->clock_domain, "async_reset_2ff.clk");
    EXPECT_EQ(meta->reset_signal, "rst_n");
    EXPECT_EQ(meta->reset_pol, ResetPolarity::ActiveLow);
    EXPECT_TRUE(meta->is_async_reset);
}

TEST(FrontendTest, SameDomainHasOneRegister) {
    opencdc::frontend::SlangAdapter adapter;
    auto result = adapter.elaborate(
        {fixture_path("same_domain.sv")}, "simple_same_domain");
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.graph.register_count(), 1u);
}

TEST(FrontendTest, AlwaysCombIsNotRegister) {
    opencdc::frontend::SlangAdapter adapter;
    auto result = adapter.elaborate(
        {fixture_path("always_comb.sv")}, "always_comb_model");
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.graph.register_count(), 1u);
    EXPECT_EQ(result.graph.find_node_by_name("always_comb_model.next_q")->kind,
              NodeKind::Net);
}

TEST(FrontendTest, Sync2ffHasTwoRegisters) {
    opencdc::frontend::SlangAdapter adapter;
    auto result = adapter.elaborate(
        {fixture_path("sync_2ff.sv")}, "sync_2ff");
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.graph.register_count(), 2u);
    EXPECT_GE(result.graph.edge_count(), 2u);
}

TEST(FrontendTest, MissingTopModuleFails) {
    opencdc::frontend::SlangAdapter adapter;
    auto result = adapter.elaborate(
        {fixture_path("cdc_crossing.sv")}, "nonexistent");
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.errors.empty());
}

TEST(FrontendTest, CdcCrossingDomainsAreCorrect) {
    opencdc::frontend::SlangAdapter adapter;
    auto result = adapter.elaborate(
        {fixture_path("cdc_crossing.sv")}, "simple_cdc_crossing");
    ASSERT_TRUE(result.ok);

    cdc_clock::DomainExtractor extractor;
    auto dom_result = extractor.extract(result.graph);

    ASSERT_EQ(dom_result.domains.size(), 2u);
    EXPECT_TRUE(dom_result.warnings.empty());

    const cdc_clock::ClockDomain* dom_a = nullptr;
    const cdc_clock::ClockDomain* dom_b = nullptr;
    for (const auto& d : dom_result.domains) {
        if (d.name == "clk_a") dom_a = &d;
        if (d.name == "clk_b") dom_b = &d;
    }
    ASSERT_NE(dom_a, nullptr);
    ASSERT_NE(dom_b, nullptr);
    EXPECT_EQ(dom_a->register_ids.size(), 1u);
    EXPECT_EQ(dom_b->register_ids.size(), 1u);
}

TEST(FrontendTest, SameDomainRegistersShareDomain) {
    opencdc::frontend::SlangAdapter adapter;
    auto result = adapter.elaborate(
        {fixture_path("same_domain.sv")}, "simple_same_domain");
    ASSERT_TRUE(result.ok);

    cdc_clock::DomainExtractor extractor;
    auto dom_result = extractor.extract(result.graph);

    ASSERT_EQ(dom_result.domains.size(), 1u);
    EXPECT_EQ(dom_result.domains[0].name, "clk_a");
    EXPECT_EQ(dom_result.domains[0].register_ids.size(), 1u);
}

TEST(FrontendTest, MultiDomainThreeDomains) {
    opencdc::frontend::SlangAdapter adapter;
    auto result = adapter.elaborate(
        {fixture_path("multi_domain.sv")}, "multi_domain");
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.graph.register_count(), 3u);

    cdc_clock::DomainExtractor extractor;
    auto dom_result = extractor.extract(result.graph);

    ASSERT_EQ(dom_result.domains.size(), 3u);
    EXPECT_TRUE(dom_result.warnings.empty());
}

TEST(FrontendTest, Sync2ffRegistersSameDomain) {
    opencdc::frontend::SlangAdapter adapter;
    auto result = adapter.elaborate(
        {fixture_path("sync_2ff.sv")}, "sync_2ff");
    ASSERT_TRUE(result.ok);

    cdc_clock::DomainExtractor extractor;
    auto dom_result = extractor.extract(result.graph);

    ASSERT_EQ(dom_result.domains.size(), 1u);
    EXPECT_EQ(dom_result.domains[0].register_ids.size(), 2u);
}

TEST(FrontendTest, SourceLocationsAreReal) {
    opencdc::frontend::SlangAdapter adapter;
    auto result = adapter.elaborate(
        {fixture_path("cdc_crossing.sv")}, "simple_cdc_crossing");
    ASSERT_TRUE(result.ok);

    for (const auto& node : result.graph.nodes()) {
        if (node.kind == NodeKind::Register) {
            EXPECT_FALSE(node.loc.file.empty()) << "File should be set for " << node.hier_name;
            EXPECT_GT(node.loc.line, 0u) << "Line should be > 0 for " << node.hier_name;
        }
    }
}

TEST(FrontendTest, ResetInfoExtracted) {
    opencdc::frontend::SlangAdapter adapter;
    auto result = adapter.elaborate(
        {fixture_path("cdc_crossing.sv")}, "simple_cdc_crossing");
    ASSERT_TRUE(result.ok);

    for (const auto& node : result.graph.nodes()) {
        if (node.kind == NodeKind::Register) {
            EXPECT_EQ(node.reset_signal, "rst_n");
            EXPECT_EQ(node.reset_pol, ResetPolarity::ActiveLow);
            EXPECT_TRUE(node.is_async_reset);
        }
    }
}

TEST(FrontendTest, CdcCrossingProducesOneFinding) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate(
        {fixture_path("cdc_crossing.sv")}, "simple_cdc_crossing");
    ASSERT_TRUE(fe.ok);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);

    cdc_ns::PatternRecognizer pattern_recognizer;
    pattern_recognizer.analyze_and_annotate(fe.graph);
    cdc_ns::CrossingAnalyzer ca;
    ca.set_pattern_recognizer(&pattern_recognizer);
    auto findings = ca.analyze(fe.graph, dr.domains, dr.register_to_domain);

    ASSERT_EQ(findings.size(), 1u);
    EXPECT_EQ(findings[0].rule_id, "CDC001");
    EXPECT_EQ(findings[0].severity, "error");
}

TEST(FrontendTest, SameDomainProducesNoFindings) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate(
        {fixture_path("same_domain.sv")}, "simple_same_domain");
    ASSERT_TRUE(fe.ok);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);

    cdc_ns::CrossingAnalyzer ca;
    auto findings = ca.analyze(fe.graph, dr.domains, dr.register_to_domain);

    EXPECT_TRUE(findings.empty());
}

TEST(FrontendTest, Sync2ffNoError) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate(
        {fixture_path("sync_2ff.sv")}, "sync_2ff");
    ASSERT_TRUE(fe.ok);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);

    cdc_ns::CrossingAnalyzer ca;
    auto findings = ca.analyze(fe.graph, dr.domains, dr.register_to_domain);

    for (const auto& f : findings) {
        EXPECT_EQ(f.detected_sync, cdc_ns::SyncPattern::TwoFF);
    }
}

TEST(FrontendTest, MultiDomainProducesMultipleFindings) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate(
        {fixture_path("multi_domain.sv")}, "multi_domain");
    ASSERT_TRUE(fe.ok);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);

    cdc_ns::CrossingAnalyzer ca;
    auto findings = ca.analyze(fe.graph, dr.domains, dr.register_to_domain);

    EXPECT_GE(findings.size(), 1u);
}

TEST(FrontendTest, Sync3ffNoError) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate(
        {fixture_path("sync_3ff.sv")}, "sync_3ff");
    ASSERT_TRUE(fe.ok);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);

    cdc_ns::CrossingAnalyzer ca;
    auto findings = ca.analyze(fe.graph, dr.domains, dr.register_to_domain);

    for (const auto& f : findings) {
        EXPECT_EQ(f.detected_sync, cdc_ns::SyncPattern::ThreeFF);
    }
}

TEST(FrontendTest, SyncMisuseDetected) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate(
        {fixture_path("sync_misuse.sv")}, "sync_misuse");
    ASSERT_TRUE(fe.ok);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);

    cdc_ns::CrossingAnalyzer ca;
    auto findings = ca.analyze(fe.graph, dr.domains, dr.register_to_domain);

    ASSERT_GE(findings.size(), 1u);
    EXPECT_EQ(findings[0].rule_id, "CDC001");
}

TEST(FrontendTest, SyncMisuseExitCode) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate(
        {fixture_path("sync_misuse.sv")}, "sync_misuse");
    ASSERT_TRUE(fe.ok);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);

    cdc_ns::CrossingAnalyzer ca;
    auto findings = ca.analyze(fe.graph, dr.domains, dr.register_to_domain);

    // sync_misuse.sv has a proper 2FF chain: src_ff -> meta -> sync_reg
    // CDC001 should be downgraded to warning since sync detected
    bool has_cdc001 = false;
    for (const auto& f : findings) {
        if (f.rule_id == "CDC001") {
            has_cdc001 = true;
            EXPECT_EQ(f.severity, "warning")
                << "CDC001 should be warning when sync chain detected";
        }
    }
    EXPECT_TRUE(has_cdc001);
}

TEST(FrontendTest, GatedClockNoFalseCrossing) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate(
        {fixture_path("gated_clock.sv")}, "gated_clock");
    ASSERT_TRUE(fe.ok);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);

    cdc_ns::CrossingAnalyzer ca;
    auto findings = ca.analyze(fe.graph, dr.domains, dr.register_to_domain);
    bool has_cdc001 = false;
    for (const auto& f : findings) {
        if (f.rule_id == "CDC001") has_cdc001 = true;
    }
    EXPECT_FALSE(has_cdc001);
}

TEST(FrontendTest, GatedCrossingDetected) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate(
        {fixture_path("gated_crossing.sv")}, "gated_crossing");
    ASSERT_TRUE(fe.ok);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);

    cdc_ns::CrossingAnalyzer ca;
    auto findings = ca.analyze(fe.graph, dr.domains, dr.register_to_domain);

    ASSERT_EQ(findings.size(), 2u);
    EXPECT_EQ(findings[0].rule_id, "CDC001");
    EXPECT_EQ(findings[0].source_domain, "clk_a");
    // dst_ff is clocked by the gated clk_b_en — CDC004 must fire for the
    // destination side too, not just the source.
    bool has_cdc004 = false;
    for (const auto& f : findings) {
        if (f.rule_id == "CDC004") has_cdc004 = true;
    }
    EXPECT_TRUE(has_cdc004);
}

TEST(FrontendTest, GatedClockResolvedInVerbose) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate(
        {fixture_path("gated_clock.sv")}, "gated_clock");
    ASSERT_TRUE(fe.ok);

    bool found_root = false;
    for (const auto& node : fe.graph.nodes()) {
        if (node.kind == NodeKind::Register && !node.root_clock.empty()) {
            found_root = true;
        }
    }
    EXPECT_TRUE(found_root);
}

TEST(FrontendTest, MuxedClockNoFalseCrossing) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate(
        {fixture_path("muxed_clock.sv")}, "muxed_clock");
    ASSERT_TRUE(fe.ok);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);

    cdc_ns::CrossingAnalyzer ca;
    auto findings = ca.analyze(fe.graph, dr.domains, dr.register_to_domain);
    EXPECT_TRUE(findings.empty());
}

TEST(FrontendTest, MuxedCrossingDetected) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate(
        {fixture_path("muxed_crossing.sv")}, "muxed_crossing");
    ASSERT_TRUE(fe.ok);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);

    cdc_ns::CrossingAnalyzer ca;
    auto findings = ca.analyze(fe.graph, dr.domains, dr.register_to_domain);

    ASSERT_EQ(findings.size(), 1u);
    EXPECT_EQ(findings[0].rule_id, "CDC001");
}

TEST(FrontendTest, ReconvergenceHazardDetected) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate(
        {fixture_path("reconvergence_hazard.sv")}, "reconvergence_hazard");
    ASSERT_TRUE(fe.ok);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);

    cdc_ns::CrossingAnalyzer ca;
    auto findings = ca.analyze(fe.graph, dr.domains, dr.register_to_domain);

    ASSERT_GE(findings.size(), 2u);
    for (const auto& f : findings) {
        EXPECT_EQ(f.rule_id, "CDC001");
    }
}

TEST(FrontendTest, ReconvergenceSafeDetected) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate(
        {fixture_path("reconvergence_safe.sv")}, "reconvergence_safe");
    ASSERT_TRUE(fe.ok);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);

    cdc_ns::CrossingAnalyzer ca;
    auto findings = ca.analyze(fe.graph, dr.domains, dr.register_to_domain);

    ASSERT_GE(findings.size(), 2u);
    for (const auto& f : findings) {
        EXPECT_EQ(f.rule_id, "CDC001");
        EXPECT_NE(f.detected_sync, cdc_ns::SyncPattern::None);
    }
}

TEST(FrontendTest, WaiverSuppressesError) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate(
        {fixture_path("cdc_crossing.sv")}, "simple_cdc_crossing");
    ASSERT_TRUE(fe.ok);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);

    cdc_ns::CrossingAnalyzer ca;
    auto findings = ca.analyze(fe.graph, dr.domains, dr.register_to_domain);
    ASSERT_EQ(findings.size(), 1u);

    cdc_ns::WaiverEngine we;
    cdc_ns::Waiver w;
    w.rule_id = "CDC001";
    w.source_reg_name = "simple_cdc_crossing.src_ff";
    w.dest_reg_name = "simple_cdc_crossing.dst_ff";
    w.source_domain = "clk_a";
    w.dest_domain = "clk_b";
    w.justification = "Test waiver";
    we.add_waiver(w);

    auto waived = we.apply(findings);
    ASSERT_EQ(waived.size(), 1u);
    EXPECT_TRUE(waived[0].waived);
    EXPECT_EQ(waived[0].waiver_justification, "Test waiver");

    opencdc::report::Reporter reporter;
    EXPECT_FALSE(reporter.has_unsuppressed_errors(waived));
}

TEST(FrontendTest, DisableRuleSuppressesFinding) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate(
        {fixture_path("cdc_crossing.sv")}, "simple_cdc_crossing");
    ASSERT_TRUE(fe.ok);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);

    cdc_ns::CrossingAnalyzer ca;
    auto findings = ca.analyze(fe.graph, dr.domains, dr.register_to_domain);
    ASSERT_EQ(findings.size(), 1u);

    opencdc::rules::RuleEngine re;
    re.add_override({"CDC001", "", true, false});
    auto filtered = re.filter(findings);
    EXPECT_TRUE(filtered.empty());
}

TEST(FrontendTest, SeverityOverrideChangesOutput) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate(
        {fixture_path("cdc_crossing.sv")}, "simple_cdc_crossing");
    ASSERT_TRUE(fe.ok);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);

    cdc_ns::CrossingAnalyzer ca;
    auto findings = ca.analyze(fe.graph, dr.domains, dr.register_to_domain);
    ASSERT_EQ(findings.size(), 1u);

    opencdc::rules::RuleEngine re;
    re.add_override({"CDC001", "warning", false, true});
    auto filtered = re.filter(findings);
    ASSERT_EQ(filtered.size(), 1u);
    EXPECT_EQ(filtered[0].severity, "warning");

    opencdc::report::Reporter reporter;
    EXPECT_FALSE(reporter.has_unsuppressed_errors(filtered));
}

TEST(FrontendTest, ReportJsonArrayFormat) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate(
        {fixture_path("cdc_crossing.sv")}, "simple_cdc_crossing");
    ASSERT_TRUE(fe.ok);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);

    cdc_ns::CrossingAnalyzer ca;
    auto findings = ca.analyze(fe.graph, dr.domains, dr.register_to_domain);

    opencdc::report::Reporter reporter;
    std::ostringstream os;
    reporter.report_json(findings, os);

    std::string out = os.str();
    EXPECT_EQ(out.front(), '[');
    EXPECT_NE(out.find("CDC001"), std::string::npos);
    EXPECT_NE(out.find("simple_cdc_crossing.src_ff"), std::string::npos);
}

TEST(FrontendTest, MultiBitCrossingDetected) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate(
        {fixture_path("multi_bit_crossing.sv")}, "multi_bit_crossing");
    ASSERT_TRUE(fe.ok);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);

    cdc_ns::CrossingAnalyzer ca;
    auto findings = ca.analyze(fe.graph, dr.domains, dr.register_to_domain);

    bool found_cdc002 = false;
    for (const auto& f : findings) {
        if (f.rule_id == "CDC002") {
            found_cdc002 = true;
            EXPECT_EQ(f.severity, "error");
            EXPECT_EQ(f.bus_width, 8u);
            EXPECT_FALSE(f.is_gray_coded);
            EXPECT_FALSE(f.has_handshake);
        }
    }
    EXPECT_TRUE(found_cdc002);
}

TEST(FrontendTest, GrayCodedCrossingNoCdc002) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate(
        {fixture_path("gray_coded_crossing.sv")}, "gray_coded_crossing");
    ASSERT_TRUE(fe.ok);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);

    cdc_ns::PatternRecognizer pattern_recognizer;
    pattern_recognizer.analyze_and_annotate(fe.graph);
    cdc_ns::CrossingAnalyzer ca;
    ca.set_pattern_recognizer(&pattern_recognizer);
    auto findings = ca.analyze(fe.graph, dr.domains, dr.register_to_domain);

    for (const auto& f : findings) {
        EXPECT_NE(f.rule_id, "CDC002") << "Gray-coded crossing should not trigger CDC002";
    }
}

TEST(FrontendTest, GatedClockCrossingDetected) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate(
        {fixture_path("gated_crossing_cdc004.sv")}, "gated_crossing_cdc004");
    ASSERT_TRUE(fe.ok);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);

    cdc_ns::CrossingAnalyzer ca;
    auto findings = ca.analyze(fe.graph, dr.domains, dr.register_to_domain);

    bool found_crossing = false;
    for (const auto& f : findings) {
        if (f.rule_id == "CDC001") {
            found_crossing = true;
        }
    }
    EXPECT_TRUE(found_crossing);
}

TEST(FrontendTest, MissingResetDetected) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate(
        {fixture_path("missing_reset_cdc007.sv")}, "missing_reset_cdc007");
    ASSERT_TRUE(fe.ok);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);

    cdc_ns::CrossingAnalyzer ca;
    auto findings = ca.analyze(fe.graph, dr.domains, dr.register_to_domain);

    bool found_cdc007 = false;
    for (const auto& f : findings) {
        if (f.rule_id == "CDC007") {
            found_cdc007 = true;
            EXPECT_EQ(f.severity, "warning");
        }
    }
    EXPECT_TRUE(found_cdc007);
}

TEST(FrontendTest, NewRulesRegisteredInEngine) {
    opencdc::rules::RuleEngine engine;
    EXPECT_TRUE(engine.find_rule("CDC004").has_value());
    EXPECT_TRUE(engine.find_rule("CDC005").has_value());
    EXPECT_TRUE(engine.find_rule("CDC006").has_value());
    EXPECT_TRUE(engine.find_rule("CDC007").has_value());
    EXPECT_TRUE(engine.find_rule("CDC008").has_value());
    EXPECT_TRUE(engine.find_rule("CDC009").has_value());

    auto cdc004 = engine.find_rule("CDC004");
    EXPECT_EQ(cdc004->name, "gated_clock_crossing");
    EXPECT_EQ(cdc004->severity, "warning");

    auto cdc007 = engine.find_rule("CDC007");
    EXPECT_EQ(cdc007->name, "missing_reset");
    EXPECT_EQ(cdc007->severity, "warning");
}

TEST(FrontendTest, WireCrossingDetected) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate(
        {fixture_path("wire_crossing.sv")}, "wire_crossing");
    ASSERT_TRUE(fe.ok);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);

    cdc_ns::CrossingAnalyzer ca;
    auto findings = ca.analyze(fe.graph, dr.domains, dr.register_to_domain);

    bool found = false;
    for (const auto& f : findings) {
        if (f.rule_id == "CDC001" &&
            f.source_reg_name.find("src_ff") != std::string::npos &&
            f.dest_reg_name.find("dst_ff") != std::string::npos) {
            found = true;
        }
    }
    EXPECT_TRUE(found) << "Crossing through wire not detected";
}

TEST(FrontendTest, CombinationalCrossingDetected) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate(
        {fixture_path("comb_crossing.sv")}, "comb_crossing");
    ASSERT_TRUE(fe.ok);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);

    cdc_ns::CrossingAnalyzer ca;
    auto findings = ca.analyze(fe.graph, dr.domains, dr.register_to_domain);

    bool found = false;
    for (const auto& f : findings) {
        if (f.rule_id == "CDC001" &&
            f.source_reg_name.find("src_reg") != std::string::npos &&
            f.dest_reg_name.find("dst_reg") != std::string::npos) {
            found = true;
        }
    }
    EXPECT_TRUE(found) << "Crossing through combinational logic not detected";
}

TEST(FrontendTest, GatedClockStructurallyDetected) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate(
        {fixture_path("gated_clock_struct.sv")}, "gated_clock_struct");
    ASSERT_TRUE(fe.ok);

    bool has_gated = false;
    for (const auto& node : fe.graph.nodes()) {
        if (node.kind == NodeKind::Register && node.clock_is_gated) {
            has_gated = true;
            size_t dot = node.root_clock.rfind('.');
            std::string short_name = (dot != std::string::npos)
                ? node.root_clock.substr(dot + 1) : node.root_clock;
            EXPECT_EQ(short_name, "clk");
        }
    }
    EXPECT_TRUE(has_gated) << "Gated clock not structurally detected";
}

TEST(FrontendTest, HierarchicalPortConnectionsPreserveCdcPath) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate(
        {fixture_path("hier_crossing.sv")}, "hier_crossing");
    ASSERT_TRUE(fe.ok);

    const auto* source = fe.graph.find_node_by_name("hier_crossing.src_ff");
    const auto* child_input = fe.graph.find_node_by_name(
        "hier_crossing.u_sync.d_in");
    const auto* child_stage = fe.graph.find_node_by_name(
        "hier_crossing.u_sync.meta_ff");
    ASSERT_NE(source, nullptr);
    ASSERT_NE(child_input, nullptr);
    ASSERT_NE(child_stage, nullptr);

    auto result = fe.graph.find_register_paths(source->id);
    bool reaches_child = false;
    for (const auto& path : result.paths) {
        if (path.dst_reg_id == child_stage->id) {
            reaches_child = true;
            break;
        }
    }
    EXPECT_TRUE(reaches_child);
}

TEST(FrontendTest, GenerateBlockCrossingDetected) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate(
        {fixture_path("generate_crossing.sv")}, "generate_crossing");
    ASSERT_TRUE(fe.ok);

    EXPECT_GE(fe.graph.register_count(), 3u);

    const auto* src = fe.graph.find_node_by_name("generate_crossing.src_ff");
    ASSERT_NE(src, nullptr);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);
    EXPECT_GE(dr.domains.size(), 2u);

    cdc_ns::PatternRecognizer pr;
    pr.analyze_and_annotate(fe.graph);
    cdc_ns::CrossingAnalyzer ca;
    ca.set_pattern_recognizer(&pr);
    auto findings = ca.analyze(fe.graph, dr.domains, dr.register_to_domain);

    bool found = false;
    for (const auto& f : findings) {
        if (f.rule_id == "CDC001") {
            found = true;
            EXPECT_EQ(f.source_domain, "clk_a");
            EXPECT_EQ(f.dest_domain, "clk_b");
        }
    }
    EXPECT_TRUE(found) << "CDC crossing inside generate block not detected";
}

TEST(FrontendTest, ComplexLvalueCrossingDetected) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate(
        {fixture_path("complex_lv_crossing.sv")}, "complex_lv_crossing");
    ASSERT_TRUE(fe.ok);

    EXPECT_GE(fe.graph.register_count(), 4u);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);
    EXPECT_GE(dr.domains.size(), 2u);

    cdc_ns::PatternRecognizer pr;
    pr.analyze_and_annotate(fe.graph);
    cdc_ns::CrossingAnalyzer ca;
    ca.set_pattern_recognizer(&pr);
    auto findings = ca.analyze(fe.graph, dr.domains, dr.register_to_domain);

    bool found = false;
    for (const auto& f : findings) {
        if (f.rule_id == "CDC001") {
            found = true;
        }
    }
    EXPECT_TRUE(found) << "Part-select crossing not detected";
}

TEST(FrontendTest, PartSelectPreservesEdgeWidth) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate(
        {fixture_path("complex_lv_crossing.sv")}, "complex_lv_crossing");
    ASSERT_TRUE(fe.ok);

    const auto* src = fe.graph.find_node_by_name("complex_lv_crossing.src_ff");
    ASSERT_NE(src, nullptr);

    bool has_8bit_edge = false;
    for (uint64_t succ : fe.graph.successors(src->id)) {
        const auto* n = fe.graph.find_node(succ);
        if (n && n->width == 8) {
            has_8bit_edge = true;
            break;
        }
    }
    EXPECT_TRUE(has_8bit_edge) << "Part-select should produce 8-bit edge";
}

TEST(FrontendTest, MultiInstanceDifferentClocksSeparateDomains) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate(
        {fixture_path("multi_instance_clock.sv")}, "multi_instance_clock");
    ASSERT_TRUE(fe.ok);
    EXPECT_EQ(fe.graph.register_count(), 2u);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);

    ASSERT_EQ(dr.domains.size(), 2u);

    const cdc_clock::ClockDomain* dom_a = nullptr;
    const cdc_clock::ClockDomain* dom_b = nullptr;
    for (const auto& d : dr.domains) {
        if (d.name == "clk_a") dom_a = &d;
        if (d.name == "clk_b") dom_b = &d;
    }
    ASSERT_NE(dom_a, nullptr);
    ASSERT_NE(dom_b, nullptr);
    EXPECT_EQ(dom_a->register_ids.size(), 1u);
    EXPECT_EQ(dom_b->register_ids.size(), 1u);
}

TEST(FrontendTest, MultiInstanceSameClockSharesDomain) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate(
        {fixture_path("multi_instance_clock.sv")}, "multi_instance_clock");
    ASSERT_TRUE(fe.ok);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);

    EXPECT_EQ(dr.domains.size(), 2u);
    bool has_shared = false;
    for (const auto& d : dr.domains) {
        if (d.register_ids.size() == 2) has_shared = true;
    }
    EXPECT_FALSE(has_shared) << "Two instances with different clocks should not share a domain";
}

TEST(FrontendTest, FuncCallCrossingDetected) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate(
        {fixture_path("func_task_crossing.sv")}, "func_task_crossing");
    ASSERT_TRUE(fe.ok);

    const auto* src = fe.graph.find_node_by_name("func_task_crossing.src_reg");
    const auto* dst = fe.graph.find_node_by_name("func_task_crossing.dst_reg");
    ASSERT_NE(src, nullptr);
    ASSERT_NE(dst, nullptr);

    bool path_exists = false;
    for (uint64_t succ : fe.graph.successors(src->id)) {
        if (succ == dst->id) { path_exists = true; break; }
    }
    for (uint64_t pred : fe.graph.predecessors(dst->id)) {
        if (pred == src->id) { path_exists = true; break; }
    }
    EXPECT_TRUE(path_exists) << "Function call should not block CDC path from src to dst";
}

TEST(FrontendTest, TaskCallCrossingDetected) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate(
        {fixture_path("func_task_crossing.sv")}, "task_crossing");
    ASSERT_TRUE(fe.ok);

    const auto* src = fe.graph.find_node_by_name("task_crossing.src_reg");
    ASSERT_NE(src, nullptr);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);

    cdc_ns::PatternRecognizer pr;
    pr.analyze_and_annotate(fe.graph);
    cdc_ns::CrossingAnalyzer ca;
    ca.set_pattern_recognizer(&pr);
    auto findings = ca.analyze(fe.graph, dr.domains, dr.register_to_domain);

    bool found = false;
    for (const auto& f : findings) {
        if (f.rule_id == "CDC001" &&
            f.source_reg_name.find("src_reg") != std::string::npos) {
            found = true;
        }
    }

    if (!found) {
        for (const auto& n : fe.graph.nodes()) {
            if (n.kind == NodeKind::Register) {
                std::cerr << "REG: " << n.hier_name << " clock=" << n.clock_domain
                          << " root=" << n.root_clock << std::endl;
            }
        }
        for (const auto& e : fe.graph.edges()) {
            auto* from = fe.graph.find_node(e.from_id);
            auto* to = fe.graph.find_node(e.to_id);
            if (from && to)
                std::cerr << "EDGE: " << from->hier_name << " -> " << to->hier_name << std::endl;
        }
        for (const auto& f : findings) {
            std::cerr << "FINDING: " << f.rule_id << " " << f.source_reg_name << " -> " << f.dest_reg_name << std::endl;
        }
    }

    EXPECT_TRUE(found) << "Task call crossing should be detected";
}
