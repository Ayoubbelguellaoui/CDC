#include <gtest/gtest.h>

#include <string>

#include "cdc/cdc006.h"
#include "cdc/crossing.h"
#include "cdc/pattern.h"
#include "cdc/reconvergence.h"
#include "cdc/reset_domain.h"
#include "cdc/waiver.h"
#include "clock/domain.h"
#include "frontend/slang_adapter.h"
#include "report/report.h"
#include "rules/rule.h"

using opencdc::ir::NodeKind;
using opencdc::ir::ResetPolarity;
namespace cdc_clock = opencdc::clock;
namespace cdc_ns = opencdc::cdc;

static std::string fixture_path(const std::string& name) {
    return std::string(FIXTURES_DIR) + "/sv/" + name;
}

TEST(FrontendTest, CdcCrossingHasTwoRegisters) {
    opencdc::frontend::SlangAdapter adapter;
    auto result = adapter.elaborate({fixture_path("cdc_crossing.sv")}, "simple_cdc_crossing");
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.graph.register_count(), 2u);
    EXPECT_GE(result.graph.edge_count(), 1u);
}

TEST(FrontendTest, AsyncResetEventListRolesClassifiedCorrectly) {
    // `@(posedge clk or negedge rst_n)`: the clock is the last non-reset
    // event and rst_n is the async reset — regardless of event order.
    opencdc::frontend::SlangAdapter adapter;
    auto result = adapter.elaborate({fixture_path("async_reset_2ff.sv")}, "async_reset_2ff");
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
    auto result = adapter.elaborate({fixture_path("same_domain.sv")}, "simple_same_domain");
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.graph.register_count(), 1u);
}

TEST(FrontendTest, AlwaysCombIsNotRegister) {
    opencdc::frontend::SlangAdapter adapter;
    auto result = adapter.elaborate({fixture_path("always_comb.sv")}, "always_comb_model");
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.graph.register_count(), 1u);
    EXPECT_EQ(result.graph.find_node_by_name("always_comb_model.next_q")->kind, NodeKind::Net);
}

TEST(FrontendTest, Sync2ffHasTwoRegisters) {
    opencdc::frontend::SlangAdapter adapter;
    auto result = adapter.elaborate({fixture_path("sync_2ff.sv")}, "sync_2ff");
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.graph.register_count(), 2u);
    EXPECT_GE(result.graph.edge_count(), 2u);
}

TEST(FrontendTest, MissingTopModuleFails) {
    opencdc::frontend::SlangAdapter adapter;
    auto result = adapter.elaborate({fixture_path("cdc_crossing.sv")}, "nonexistent");
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.errors.empty());
}

TEST(FrontendTest, CdcCrossingDomainsAreCorrect) {
    opencdc::frontend::SlangAdapter adapter;
    auto result = adapter.elaborate({fixture_path("cdc_crossing.sv")}, "simple_cdc_crossing");
    ASSERT_TRUE(result.ok);

    cdc_clock::DomainExtractor extractor;
    auto dom_result = extractor.extract(result.graph);

    ASSERT_EQ(dom_result.domains.size(), 2u);
    EXPECT_TRUE(dom_result.warnings.empty());

    const cdc_clock::ClockDomain* dom_a = nullptr;
    const cdc_clock::ClockDomain* dom_b = nullptr;
    for (const auto& d : dom_result.domains) {
        if (d.name == "clk_a")
            dom_a = &d;
        if (d.name == "clk_b")
            dom_b = &d;
    }
    ASSERT_NE(dom_a, nullptr);
    ASSERT_NE(dom_b, nullptr);
    EXPECT_EQ(dom_a->register_ids.size(), 1u);
    EXPECT_EQ(dom_b->register_ids.size(), 1u);
}

TEST(FrontendTest, SameDomainRegistersShareDomain) {
    opencdc::frontend::SlangAdapter adapter;
    auto result = adapter.elaborate({fixture_path("same_domain.sv")}, "simple_same_domain");
    ASSERT_TRUE(result.ok);

    cdc_clock::DomainExtractor extractor;
    auto dom_result = extractor.extract(result.graph);

    ASSERT_EQ(dom_result.domains.size(), 1u);
    EXPECT_EQ(dom_result.domains[0].name, "clk_a");
    EXPECT_EQ(dom_result.domains[0].register_ids.size(), 1u);
}

TEST(FrontendTest, MultiDomainThreeDomains) {
    opencdc::frontend::SlangAdapter adapter;
    auto result = adapter.elaborate({fixture_path("multi_domain.sv")}, "multi_domain");
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.graph.register_count(), 3u);

    cdc_clock::DomainExtractor extractor;
    auto dom_result = extractor.extract(result.graph);

    ASSERT_EQ(dom_result.domains.size(), 3u);
    EXPECT_TRUE(dom_result.warnings.empty());
}

TEST(FrontendTest, Sync2ffRegistersSameDomain) {
    opencdc::frontend::SlangAdapter adapter;
    auto result = adapter.elaborate({fixture_path("sync_2ff.sv")}, "sync_2ff");
    ASSERT_TRUE(result.ok);

    cdc_clock::DomainExtractor extractor;
    auto dom_result = extractor.extract(result.graph);

    ASSERT_EQ(dom_result.domains.size(), 1u);
    EXPECT_EQ(dom_result.domains[0].register_ids.size(), 2u);
}

TEST(FrontendTest, SourceLocationsAreReal) {
    opencdc::frontend::SlangAdapter adapter;
    auto result = adapter.elaborate({fixture_path("cdc_crossing.sv")}, "simple_cdc_crossing");
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
    auto result = adapter.elaborate({fixture_path("cdc_crossing.sv")}, "simple_cdc_crossing");
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
    auto fe = adapter.elaborate({fixture_path("cdc_crossing.sv")}, "simple_cdc_crossing");
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
    auto fe = adapter.elaborate({fixture_path("same_domain.sv")}, "simple_same_domain");
    ASSERT_TRUE(fe.ok);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);

    cdc_ns::CrossingAnalyzer ca;
    auto findings = ca.analyze(fe.graph, dr.domains, dr.register_to_domain);

    EXPECT_TRUE(findings.empty());
}

TEST(FrontendTest, Sync2ffNoError) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate({fixture_path("sync_2ff.sv")}, "sync_2ff");
    ASSERT_TRUE(fe.ok);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);

    cdc_ns::CrossingAnalyzer ca;
    auto findings = ca.analyze(fe.graph, dr.domains, dr.register_to_domain);

    for (const auto& f : findings) {
        EXPECT_EQ(f.detected_sync, cdc_ns::SyncPattern::TwoFF);
    }
}

TEST(FrontendTest, PackedShiftChainTaggedOnAst) {
    // `sync_b <= {sync_b[0], reg_a}` is a verified intra-vector shift chain
    // and tags the node; the reversed `hold_b <= {reg_a, hold_b[0]}` is not.
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate({fixture_path("packed_sync.sv")}, "packed_sync");
    ASSERT_TRUE(fe.ok);

    const auto* sync_b = fe.graph.find_node_by_name("packed_sync.sync_b");
    ASSERT_NE(sync_b, nullptr);
    EXPECT_EQ(sync_b->kind, NodeKind::Register);
    EXPECT_EQ(sync_b->width, 2u);
    EXPECT_EQ(sync_b->packed_shift_depth, 2u);

    const auto* hold_b = fe.graph.find_node_by_name("packed_sync.hold_b");
    ASSERT_NE(hold_b, nullptr);
    EXPECT_EQ(hold_b->width, 2u);
    EXPECT_EQ(hold_b->packed_shift_depth, 0u);
}

TEST(FrontendTest, PackedSync2ffNoError) {
    // The packed-vector form must analyze exactly like the scalar two
    // register form: CDC001 downgraded to info/VerifiedSafe with a TwoFF
    // chain detected at the destination. The reversed (non-chain) vector in
    // the same fixture must keep its error.
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate({fixture_path("packed_sync.sv")}, "packed_sync");
    ASSERT_TRUE(fe.ok);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);

    cdc_ns::CrossingAnalyzer ca;
    auto findings = ca.analyze(fe.graph, dr.domains, dr.register_to_domain);

    const cdc_ns::Finding* packed = nullptr;
    const cdc_ns::Finding* hold = nullptr;
    for (const auto& f : findings) {
        if (f.rule_id != "CDC001")
            continue;
        if (f.dest_reg_name.find("sync_b") != std::string::npos)
            packed = &f;
        else if (f.dest_reg_name.find("hold_b") != std::string::npos)
            hold = &f;
    }
    ASSERT_NE(packed, nullptr);
    EXPECT_EQ(packed->severity, "info");
    EXPECT_EQ(packed->detected_sync, cdc_ns::SyncPattern::TwoFF);
    EXPECT_EQ(packed->safety_status, cdc_ns::SafetyStatus::VerifiedSafe);
    ASSERT_NE(hold, nullptr);
    EXPECT_EQ(hold->severity, "error");
    EXPECT_EQ(hold->safety_status, cdc_ns::SafetyStatus::VerifiedUnsafe);
}

TEST(FrontendTest, MultiDomainProducesMultipleFindings) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate({fixture_path("multi_domain.sv")}, "multi_domain");
    ASSERT_TRUE(fe.ok);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);

    cdc_ns::CrossingAnalyzer ca;
    auto findings = ca.analyze(fe.graph, dr.domains, dr.register_to_domain);

    EXPECT_GE(findings.size(), 1u);
}

TEST(FrontendTest, Sync3ffNoError) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate({fixture_path("sync_3ff.sv")}, "sync_3ff");
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
    auto fe = adapter.elaborate({fixture_path("sync_misuse.sv")}, "sync_misuse");
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
    auto fe = adapter.elaborate({fixture_path("sync_misuse.sv")}, "sync_misuse");
    ASSERT_TRUE(fe.ok);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);

    cdc_ns::CrossingAnalyzer ca;
    auto findings = ca.analyze(fe.graph, dr.domains, dr.register_to_domain);

    // sync_misuse.sv has an 8-bit bus through a 2FF chain.
    // Width validation rejects this as a valid sync pattern (multi-bit sync is unsafe).
    // CDC001 should remain error (not downgraded to warning).
    bool has_cdc001 = false;
    for (const auto& f : findings) {
        if (f.rule_id == "CDC001") {
            has_cdc001 = true;
            EXPECT_EQ(f.severity, "error")
                << "CDC001 should be error for multi-bit bus through 2FF chain";
        }
    }
    EXPECT_TRUE(has_cdc001);
}

TEST(FrontendTest, GatedClockNoFalseCrossing) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate({fixture_path("gated_clock.sv")}, "gated_clock");
    ASSERT_TRUE(fe.ok);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);

    cdc_ns::CrossingAnalyzer ca;
    auto findings = ca.analyze(fe.graph, dr.domains, dr.register_to_domain);
    bool has_cdc001 = false;
    for (const auto& f : findings) {
        if (f.rule_id == "CDC001")
            has_cdc001 = true;
    }
    EXPECT_FALSE(has_cdc001);
}

TEST(FrontendTest, GatedCrossingDetected) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate({fixture_path("gated_crossing.sv")}, "gated_crossing");
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
        if (f.rule_id == "CDC004")
            has_cdc004 = true;
    }
    EXPECT_TRUE(has_cdc004);
}

TEST(FrontendTest, GatedClockResolvedInVerbose) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate({fixture_path("gated_clock.sv")}, "gated_clock");
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
    auto fe = adapter.elaborate({fixture_path("muxed_clock.sv")}, "muxed_clock");
    ASSERT_TRUE(fe.ok);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);

    cdc_ns::CrossingAnalyzer ca;
    auto findings = ca.analyze(fe.graph, dr.domains, dr.register_to_domain);
    EXPECT_TRUE(findings.empty());
}

TEST(FrontendTest, MuxedCrossingDetected) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate({fixture_path("muxed_crossing.sv")}, "muxed_crossing");
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
    auto fe = adapter.elaborate({fixture_path("reconvergence_hazard.sv")}, "reconvergence_hazard");
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
    auto fe = adapter.elaborate({fixture_path("reconvergence_safe.sv")}, "reconvergence_safe");
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
    auto fe = adapter.elaborate({fixture_path("cdc_crossing.sv")}, "simple_cdc_crossing");
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
    auto fe = adapter.elaborate({fixture_path("cdc_crossing.sv")}, "simple_cdc_crossing");
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
    auto fe = adapter.elaborate({fixture_path("cdc_crossing.sv")}, "simple_cdc_crossing");
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

TEST(FrontendTest, ReportJsonEnvelopeFormat) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate({fixture_path("cdc_crossing.sv")}, "simple_cdc_crossing");
    ASSERT_TRUE(fe.ok);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);

    cdc_ns::CrossingAnalyzer ca;
    auto findings = ca.analyze(fe.graph, dr.domains, dr.register_to_domain);

    opencdc::report::Reporter reporter;
    std::ostringstream os;
    reporter.report_json(findings, os);

    std::string out = os.str();
    EXPECT_EQ(out.front(), '{');
    EXPECT_NE(out.find("\"analysis_status\""), std::string::npos);
    EXPECT_NE(out.find("\"findings\""), std::string::npos);
    EXPECT_NE(out.find("CDC001"), std::string::npos);
    EXPECT_NE(out.find("simple_cdc_crossing.src_ff"), std::string::npos);
}

TEST(FrontendTest, MultiBitCrossingDetected) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate({fixture_path("multi_bit_crossing.sv")}, "multi_bit_crossing");
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
    auto fe = adapter.elaborate({fixture_path("gray_coded_crossing.sv")}, "gray_coded_crossing");
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
    auto fe =
        adapter.elaborate({fixture_path("gated_crossing_cdc004.sv")}, "gated_crossing_cdc004");
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
    auto fe = adapter.elaborate({fixture_path("missing_reset_cdc007.sv")}, "missing_reset_cdc007");
    ASSERT_TRUE(fe.ok);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);

    cdc_ns::CrossingAnalyzer ca;
    auto findings = ca.analyze(fe.graph, dr.domains, dr.register_to_domain);

    bool found_cdc007 = false;
    for (const auto& f : findings) {
        if (f.rule_id == "CDC007") {
            found_cdc007 = true;
            EXPECT_EQ(f.severity, "info");
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
    EXPECT_EQ(cdc007->severity, "info");
}

TEST(FrontendTest, WireCrossingDetected) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate({fixture_path("wire_crossing.sv")}, "wire_crossing");
    ASSERT_TRUE(fe.ok);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);

    cdc_ns::CrossingAnalyzer ca;
    auto findings = ca.analyze(fe.graph, dr.domains, dr.register_to_domain);

    bool found = false;
    for (const auto& f : findings) {
        if (f.rule_id == "CDC001" && f.source_reg_name.find("src_ff") != std::string::npos &&
            f.dest_reg_name.find("dst_ff") != std::string::npos) {
            found = true;
        }
    }
    EXPECT_TRUE(found) << "Crossing through wire not detected";
}

TEST(FrontendTest, CombinationalCrossingDetected) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate({fixture_path("comb_crossing.sv")}, "comb_crossing");
    ASSERT_TRUE(fe.ok);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);

    cdc_ns::CrossingAnalyzer ca;
    auto findings = ca.analyze(fe.graph, dr.domains, dr.register_to_domain);

    bool found = false;
    for (const auto& f : findings) {
        if (f.rule_id == "CDC001" && f.source_reg_name.find("src_reg") != std::string::npos &&
            f.dest_reg_name.find("dst_reg") != std::string::npos) {
            found = true;
        }
    }
    EXPECT_TRUE(found) << "Crossing through combinational logic not detected";
}

TEST(FrontendTest, GatedClockStructurallyDetected) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate({fixture_path("gated_clock_struct.sv")}, "gated_clock_struct");
    ASSERT_TRUE(fe.ok);

    bool has_gated = false;
    for (const auto& node : fe.graph.nodes()) {
        if (node.kind == NodeKind::Register && node.clock_is_gated) {
            has_gated = true;
            size_t dot = node.root_clock.rfind('.');
            std::string short_name =
                (dot != std::string::npos) ? node.root_clock.substr(dot + 1) : node.root_clock;
            EXPECT_EQ(short_name, "clk");
        }
    }
    EXPECT_TRUE(has_gated) << "Gated clock not structurally detected";
}

TEST(FrontendTest, HierarchicalPortConnectionsPreserveCdcPath) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate({fixture_path("hier_crossing.sv")}, "hier_crossing");
    ASSERT_TRUE(fe.ok);

    const auto* source = fe.graph.find_node_by_name("hier_crossing.src_ff");
    const auto* child_input = fe.graph.find_node_by_name("hier_crossing.u_sync.d_in");
    const auto* child_stage = fe.graph.find_node_by_name("hier_crossing.u_sync.meta_ff");
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
    auto fe = adapter.elaborate({fixture_path("generate_crossing.sv")}, "generate_crossing");
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
    auto fe = adapter.elaborate({fixture_path("complex_lv_crossing.sv")}, "complex_lv_crossing");
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
    auto fe = adapter.elaborate({fixture_path("complex_lv_crossing.sv")}, "complex_lv_crossing");
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
    auto fe = adapter.elaborate({fixture_path("multi_instance_clock.sv")}, "multi_instance_clock");
    ASSERT_TRUE(fe.ok);
    EXPECT_EQ(fe.graph.register_count(), 2u);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);

    ASSERT_EQ(dr.domains.size(), 2u);

    const cdc_clock::ClockDomain* dom_a = nullptr;
    const cdc_clock::ClockDomain* dom_b = nullptr;
    for (const auto& d : dr.domains) {
        if (d.name == "clk_a")
            dom_a = &d;
        if (d.name == "clk_b")
            dom_b = &d;
    }
    ASSERT_NE(dom_a, nullptr);
    ASSERT_NE(dom_b, nullptr);
    EXPECT_EQ(dom_a->register_ids.size(), 1u);
    EXPECT_EQ(dom_b->register_ids.size(), 1u);
}

TEST(FrontendTest, MultiInstanceSameClockSharesDomain) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate({fixture_path("multi_instance_clock.sv")}, "multi_instance_clock");
    ASSERT_TRUE(fe.ok);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);

    EXPECT_EQ(dr.domains.size(), 2u);
    bool has_shared = false;
    for (const auto& d : dr.domains) {
        if (d.register_ids.size() == 2)
            has_shared = true;
    }
    EXPECT_FALSE(has_shared) << "Two instances with different clocks should not share a domain";
}

TEST(FrontendTest, FuncCallCrossingDetected) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate({fixture_path("func_task_crossing.sv")}, "func_task_crossing");
    ASSERT_TRUE(fe.ok);

    const auto* src = fe.graph.find_node_by_name("func_task_crossing.src_reg");
    const auto* dst = fe.graph.find_node_by_name("func_task_crossing.dst_reg");
    ASSERT_NE(src, nullptr);
    ASSERT_NE(dst, nullptr);

    bool path_exists = false;
    for (uint64_t succ : fe.graph.successors(src->id)) {
        if (succ == dst->id) {
            path_exists = true;
            break;
        }
    }
    for (uint64_t pred : fe.graph.predecessors(dst->id)) {
        if (pred == src->id) {
            path_exists = true;
            break;
        }
    }
    EXPECT_TRUE(path_exists) << "Function call should not block CDC path from src to dst";
}

TEST(FrontendTest, TaskCallCrossingDetected) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate({fixture_path("func_task_crossing.sv")}, "task_crossing");
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
        if (f.rule_id == "CDC001" && f.source_reg_name.find("src_reg") != std::string::npos) {
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
            std::cerr << "FINDING: " << f.rule_id << " " << f.source_reg_name << " -> "
                      << f.dest_reg_name << std::endl;
        }
    }

    EXPECT_TRUE(found) << "Task call crossing should be detected";
}

TEST(FrontendTest, NestedInstanceHasModuleType) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate({fixture_path("hier_crossing.sv")}, "hier_crossing");
    ASSERT_TRUE(fe.ok);
    const auto* child = fe.graph.find_node_by_name("hier_crossing.u_sync.meta_ff");
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->short_name, "meta_ff");
    EXPECT_EQ(child->module_type, "child_sync");
}

TEST(FrontendTest, ToggleUnaryCreatesNotNode) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate({fixture_path("toggle_sync.sv")}, "toggle_sync");
    ASSERT_TRUE(fe.ok);
    bool found_not = false;
    for (const auto& n : fe.graph.nodes()) {
        if (n.kind == NodeKind::Combinational && n.logic_type == opencdc::ir::LogicType::Not)
            found_not = true;
    }
    EXPECT_TRUE(found_not);
}

TEST(FrontendTest, GenerateLoopIterationsAreSeparateCopies) {
    // Each loop iteration is its own scope g[i] (an empty entry name merged
    // all copies into one node), and the loop reads the module's signals:
    // clk_a reaches the instance clock (no domain of the instance port) and
    // b_q reads the module register a_m, a crossing per iteration.
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate({fixture_path("generate_scope.sv")}, "generate_scope");
    ASSERT_TRUE(fe.ok);
    EXPECT_EQ(fe.graph.register_count(), 9u);

    for (int i = 0; i < 4; ++i) {
        std::string g = "generate_scope.g[" + std::to_string(i) + "]";
        const auto* b_q = fe.graph.find_node_by_name(g + ".b_q");
        ASSERT_NE(b_q, nullptr) << g;
        EXPECT_EQ(b_q->kind, NodeKind::Register);
        EXPECT_EQ(b_q->clock_domain, "generate_scope.clk_b");
        const auto* a_q = fe.graph.find_node_by_name(g + ".u_a.q");
        ASSERT_NE(a_q, nullptr) << g;
        EXPECT_EQ(a_q->kind, NodeKind::Register);
    }
    EXPECT_EQ(fe.graph.find_node_by_name("generate_scope.g..b_q"), nullptr);
    EXPECT_EQ(fe.graph.find_node_by_name("generate_scope.g[0].a_m"), nullptr);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);
    EXPECT_EQ(dr.domains.size(), 2u);

    cdc_ns::CrossingAnalyzer ca;
    auto findings = ca.analyze(fe.graph, dr.domains, dr.register_to_domain);
    for (int i = 0; i < 4; ++i) {
        std::string dest = "generate_scope.g[" + std::to_string(i) + "].b_q";
        bool found = false;
        for (const auto& f : findings)
            found = found || (f.rule_id == "CDC001" && f.dest_reg_name == dest);
        EXPECT_TRUE(found) << dest;
    }
}

TEST(FrontendTest, UnpackedArrayPortsHaveWidth) {
    // getBitWidth() is zero for unpacked types: ports, nets and registers of
    // unpacked arrays got width 0 and failed graph validation.
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate({fixture_path("unpacked_array.sv")}, "uarr_port");
    ASSERT_TRUE(fe.ok) << (fe.errors.empty() ? "" : fe.errors.front());
    for (const auto& n : fe.graph.nodes())
        EXPECT_GT(n.width, 0u) << n.hier_name;
    const auto* din = fe.graph.find_node_by_name("uarr_port.u_child.din");
    ASSERT_NE(din, nullptr);
    EXPECT_EQ(din->width, 16u);
    const auto* mem = fe.graph.find_node_by_name("uarr_port.mem");
    ASSERT_NE(mem, nullptr);
    EXPECT_EQ(mem->kind, NodeKind::Register);
    EXPECT_EQ(mem->width, 16u);
}

TEST(FrontendTest, UnpackedArraySynchronizerIsAChain) {
    // Every access to s is a static index: s[0] and s[1] are registers of
    // their own and the 2FF chain is recognized (CDC001 info, not error).
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate({fixture_path("unpacked_array.sv")}, "uarr_sync");
    ASSERT_TRUE(fe.ok);
    EXPECT_EQ(fe.graph.find_node_by_name("uarr_sync.s"), nullptr);
    for (const char* name : {"uarr_sync.s[0]", "uarr_sync.s[1]"}) {
        const auto* n = fe.graph.find_node_by_name(name);
        ASSERT_NE(n, nullptr) << name;
        EXPECT_EQ(n->kind, NodeKind::Register);
        EXPECT_EQ(n->width, 1u);
    }

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);
    cdc_ns::CrossingAnalyzer ca;
    auto findings = ca.analyze(fe.graph, dr.domains, dr.register_to_domain);
    const cdc_ns::Finding* cross = nullptr;
    for (const auto& f : findings) {
        if (f.rule_id == "CDC001" && f.dest_reg_name == "uarr_sync.s[0]")
            cross = &f;
    }
    ASSERT_NE(cross, nullptr);
    EXPECT_EQ(cross->severity, "info");
    EXPECT_EQ(cross->detected_sync, cdc_ns::SyncPattern::TwoFF);
}

TEST(FrontendTest, UnpackedArrayWithVariableIndexStaysOneNode) {
    // rf[sel] reads an element only known at run time: splitting rf would
    // disconnect that read, so rf stays one node and the crossing is kept.
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate({fixture_path("unpacked_array.sv")}, "uarr_mixed");
    ASSERT_TRUE(fe.ok);
    const auto* rf = fe.graph.find_node_by_name("uarr_mixed.rf");
    ASSERT_NE(rf, nullptr);
    EXPECT_EQ(rf->kind, NodeKind::Register);
    EXPECT_EQ(fe.graph.find_node_by_name("uarr_mixed.rf[0]"), nullptr);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);
    cdc_ns::CrossingAnalyzer ca;
    auto findings = ca.analyze(fe.graph, dr.domains, dr.register_to_domain);
    bool error = false;
    for (const auto& f : findings)
        error = error || (f.rule_id == "CDC001" && f.dest_reg_name == "uarr_mixed.b_r" &&
                          f.severity == "error");
    EXPECT_TRUE(error);
}

TEST(FrontendTest, ParameterIsNoSignal) {
    // `d & MASK`: MASK is a constant of the elaboration, not a net.
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate({fixture_path("unpacked_array.sv")}, "uarr_param");
    ASSERT_TRUE(fe.ok);
    EXPECT_EQ(fe.graph.find_node_by_name("uarr_param.MASK"), nullptr);
    EXPECT_NE(fe.graph.find_node_by_name("uarr_param.q"), nullptr);
}

namespace {
// Elaborates and analyzes like the CLI: patterns annotated, recognizer set.
std::vector<cdc_ns::Finding> analyze_fixture(
    const std::string& file, const std::string& top, opencdc::frontend::FrontendResult& fe,
    const opencdc::frontend::FrontendOptions& options = {}) {
    opencdc::frontend::SlangAdapter adapter;
    fe = adapter.elaborate({fixture_path(file)}, top, options);
    if (!fe.ok)
        return {};
    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);
    cdc_ns::PatternRecognizer recognizer;
    recognizer.analyze_and_annotate(fe.graph);
    cdc_ns::CrossingAnalyzer ca;
    ca.set_pattern_recognizer(&recognizer);
    return ca.analyze(fe.graph, dr.domains, dr.register_to_domain);
}
}  // namespace

TEST(FrontendTest, OutputPortReachesParent) {
    // A child register drives a parent register of another clock through
    // an output port: the output connection must reach the parent net.
    opencdc::frontend::FrontendResult fe;
    auto findings = analyze_fixture("output_port.sv", "op_cross", fe);
    ASSERT_TRUE(fe.ok);
    bool found = false;
    for (const auto& f : findings)
        found = found || (f.rule_id == "CDC001" && f.source_reg_name == "op_cross.u_a.q" &&
                          f.dest_reg_name == "op_cross.q" && f.severity == "error");
    EXPECT_TRUE(found);
}

TEST(FrontendTest, ClockGateInstanceKeepsRootClock) {
    // clk_o of a clock gate instance is clk_i gated: one domain, no
    // crossing (the output port was taken for a root clock of its own).
    opencdc::frontend::FrontendResult fe;
    auto findings = analyze_fixture("output_port.sv", "op_cgate", fe);
    ASSERT_TRUE(fe.ok);
    const auto* q = fe.graph.find_node_by_name("op_cgate.q");
    ASSERT_NE(q, nullptr);
    EXPECT_EQ(q->root_clock, "clk_i");
    EXPECT_TRUE(q->clock_is_gated);
    cdc_clock::DomainExtractor de;
    EXPECT_EQ(de.extract(fe.graph).domains.size(), 1u);
    for (const auto& f : findings)
        EXPECT_NE(f.rule_id, "CDC001") << f.reason;
}

TEST(FrontendTest, PathSearchIsLinearInDiamonds) {
    // 7 four-way diamonds give 4^7 simple paths to one register, above the
    // 10000-path limit: the search keeps one path per destination and is
    // not truncated.
    opencdc::frontend::FrontendResult fe;
    auto findings = analyze_fixture("output_port.sv", "op_diamonds", fe);
    ASSERT_TRUE(fe.ok);
    bool crossing = false;
    for (const auto& f : findings) {
        EXPECT_NE(f.rule_id, "CDC010") << f.reason;
        crossing = crossing || (f.rule_id == "CDC001" && f.dest_reg_name == "op_diamonds.q");
    }
    EXPECT_TRUE(crossing);
}

namespace {
// Severity of the CDC001 (or CDC002) finding into `dest`; empty when none.
std::string severity_into(const std::vector<cdc_ns::Finding>& findings, const std::string& rule,
                          const std::string& dest) {
    for (const auto& f : findings) {
        if (f.rule_id == rule && f.dest_reg_name == dest)
            return f.severity;
    }
    return "";
}
}  // namespace

TEST(FrontendTest, LoopShiftedArraySynchronizerIsAChain) {
    // `for (int i = 1; i < STAGES; i++) s[i] <= s[i-1];` is unrolled: the
    // elements are the stages of a 2FF chain.
    opencdc::frontend::FrontendResult fe;
    auto findings = analyze_fixture("fifo_sync.sv", "fs_loop_sync", fe);
    ASSERT_TRUE(fe.ok);
    ASSERT_NE(fe.graph.find_node_by_name("fs_loop_sync.u_sync.s[1]"), nullptr);
    EXPECT_EQ(severity_into(findings, "CDC001", "fs_loop_sync.u_sync.s[0]"), "info");
}

TEST(FrontendTest, BitwiseGrayPointerSynchronizedByWideChain) {
    // A gray pointer built bit by bit in a function, assigned continuously
    // and registered, through a 5-bit 2FF chain: a verified synchronizer.
    opencdc::frontend::FrontendResult fe;
    auto findings = analyze_fixture("fifo_sync.sv", "fs_gray_ptr", fe);
    ASSERT_TRUE(fe.ok);
    const auto* g = fe.graph.find_node_by_name("fs_gray_ptr.cnt_g_r");
    ASSERT_NE(g, nullptr);
    EXPECT_TRUE(g->is_gray_coded);
    EXPECT_EQ(severity_into(findings, "CDC001", "fs_gray_ptr.u_sync.s[0]"), "info");
    EXPECT_EQ(severity_into(findings, "CDC002", "fs_gray_ptr.u_sync.s[0]"), "");
}

TEST(FrontendTest, GrayDecodedBeforeCrossingIsMultiBit) {
    // A gray register decoded to binary before it crosses: the crossing
    // carries binary, so CDC001 and CDC002 stay errors.
    opencdc::frontend::FrontendResult fe;
    auto findings = analyze_fixture("fifo_sync.sv", "fs_gray_decoded", fe);
    ASSERT_TRUE(fe.ok);
    EXPECT_EQ(severity_into(findings, "CDC001", "fs_gray_decoded.u_sync.s[0]"), "error");
    EXPECT_EQ(severity_into(findings, "CDC002", "fs_gray_decoded.u_sync.s[0]"), "error");
}

TEST(FrontendTest, BinaryPointerThroughWideChainIsUnsafe) {
    opencdc::frontend::FrontendResult fe;
    auto findings = analyze_fixture("fifo_sync.sv", "fs_bin_ptr", fe);
    ASSERT_TRUE(fe.ok);
    EXPECT_EQ(severity_into(findings, "CDC001", "fs_bin_ptr.u_sync.s[0]"), "error");
    EXPECT_EQ(severity_into(findings, "CDC002", "fs_bin_ptr.u_sync.s[0]"), "error");
}

TEST(FrontendTest, BlackBoxPllOutputsAreClockSources) {
    // Undriven outputs of a black-box PLL, through a buffer instance and
    // into a submodule port: one domain per PLL output, named by its path.
    opencdc::frontend::FrontendResult fe;
    auto findings = analyze_fixture("fifo_sync.sv", "fs_pll", fe);
    ASSERT_TRUE(fe.ok);
    const auto* a = fe.graph.find_node_by_name("fs_pll.a_r");
    const auto* s1 = fe.graph.find_node_by_name("fs_pll.u_sync.s[1]");
    ASSERT_NE(a, nullptr);
    ASSERT_NE(s1, nullptr);
    EXPECT_EQ(a->root_clock, "fs_pll.u_pll.clk0");
    EXPECT_EQ(s1->root_clock, "fs_pll.u_pll.clk1");
    cdc_clock::DomainExtractor de;
    EXPECT_EQ(de.extract(fe.graph).domains.size(), 2u);
    EXPECT_EQ(severity_into(findings, "CDC001", "fs_pll.u_sync.s[0]"), "info");
}

namespace {
// CDC009 findings of `top` in reset_hier.sv, same-clock crossings checked.
std::vector<cdc_ns::Finding> reset_crossings(const std::string& top,
                                             opencdc::frontend::FrontendResult& fe) {
    opencdc::frontend::SlangAdapter adapter;
    fe = adapter.elaborate({fixture_path("reset_hier.sv")}, top);
    if (!fe.ok)
        return {};
    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);
    cdc_ns::ResetDomainAnalyzer ra;
    auto rd = ra.extract_reset_domains(fe.graph);
    opencdc::config::ResetPolicyConfig policy;
    policy.check_same_clock_reset_crossings = true;
    return ra.check_reset_crossings(fe.graph, rd.domains, dr.domains, dr.register_to_domain,
                                    &policy);
}
}  // namespace

TEST(FrontendTest, ResetRenamedByPortIsOneResetDomain) {
    // One reset net, i_reset_n in one child and rst_n in the other: the
    // reset is named after its source, so no reset crossing.
    opencdc::frontend::FrontendResult fe;
    auto findings = reset_crossings("rh_rename", fe);
    ASSERT_TRUE(fe.ok);
    const auto* a = fe.graph.find_node_by_name("rh_rename.u_a.q");
    const auto* b = fe.graph.find_node_by_name("rh_rename.u_b.q");
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(a->reset_signal, "rst_n");
    EXPECT_EQ(b->reset_signal, "rst_n");
    for (const auto& f : findings)
        EXPECT_NE(f.rule_id, "CDC009") << f.reason;
}

TEST(FrontendTest, TwoResetsBehindOnePortNameCross) {
    // The top reset and its synchronized copy both enter a child as rst_n:
    // two reset domains, and u_a.q -> u_b.q crosses them.
    opencdc::frontend::FrontendResult fe;
    auto findings = reset_crossings("rh_two", fe);
    ASSERT_TRUE(fe.ok);
    const auto* b = fe.graph.find_node_by_name("rh_two.u_b.q");
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->reset_signal, "rh_two.rst_sync");
    bool crossing = false;
    for (const auto& f : findings)
        crossing = crossing || (f.rule_id == "CDC009" && f.source_reg_name == "rh_two.u_a.q" &&
                                f.dest_reg_name == "rh_two.u_b.q");
    EXPECT_TRUE(crossing);
}

TEST(FrontendTest, CrossingThroughAnEnable) {
    // `if (a_r) q <= 1'b1;`: a_r reaches q only through the condition.
    opencdc::frontend::FrontendResult fe;
    auto findings = analyze_fixture("control_dep.sv", "cd_enable", fe);
    ASSERT_TRUE(fe.ok);
    const auto* a = fe.graph.find_node_by_name("cd_enable.a_r");
    const auto* q = fe.graph.find_node_by_name("cd_enable.q");
    ASSERT_NE(a, nullptr);
    ASSERT_NE(q, nullptr);
    EXPECT_EQ(fe.graph.edge_role(a->id, q->id), opencdc::ir::EdgeRole::Control);
    EXPECT_EQ(severity_into(findings, "CDC001", "cd_enable.q"), "error");
}

TEST(FrontendTest, EnabledSynchronizerIsStillAChain) {
    // A same-clock enable on both stages is a Control edge: no stage
    // predecessor, the 2FF chain stays verified.
    opencdc::frontend::FrontendResult fe;
    auto findings = analyze_fixture("control_dep.sv", "cd_sync_enable", fe);
    ASSERT_TRUE(fe.ok);
    EXPECT_EQ(severity_into(findings, "CDC001", "cd_sync_enable.s0"), "info");
}

TEST(FrontendTest, ConditionGatedConsumerIsNoStage) {
    // ack depends on s1 only through `if (s1 != ack)`: not a third stage,
    // so no CDC006 and the chain stays a verified 2FF.
    opencdc::frontend::FrontendResult fe;
    auto findings = analyze_fixture("control_dep.sv", "cd_gated_consumer", fe);
    ASSERT_TRUE(fe.ok);
    EXPECT_EQ(severity_into(findings, "CDC001", "cd_gated_consumer.s0"), "info");
    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);
    cdc_ns::Cdc006Analyzer cdc006;
    EXPECT_TRUE(cdc006.analyze(fe.graph, dr.domains, dr.register_to_domain).empty());
}

TEST(FrontendTest, FirstStageReadThroughLogicIsFlagged) {
    // The still-metastable first stage feeds logic into another register.
    opencdc::frontend::FrontendResult fe;
    auto findings = analyze_fixture("control_dep.sv", "cd_tap_logic", fe);
    ASSERT_TRUE(fe.ok);
    EXPECT_EQ(severity_into(findings, "CDC001", "cd_tap_logic.s0"), "warning");
}

TEST(FrontendTest, FirstStageReadByOutputPortIsFlagged) {
    opencdc::frontend::FrontendResult fe;
    auto findings = analyze_fixture("control_dep.sv", "cd_tap_port", fe);
    ASSERT_TRUE(fe.ok);
    EXPECT_EQ(severity_into(findings, "CDC001", "cd_tap_port.s0"), "warning");
}

TEST(FrontendTest, SecondStageUsedAsConditionIsNoFanout) {
    // ack copies s1 (so it looks like a third stage) and s1 enables a
    // capture: a condition use of the synchronized output, no warning.
    opencdc::frontend::FrontendResult fe;
    auto findings = analyze_fixture("control_dep.sv", "cd_stage2_condition", fe);
    ASSERT_TRUE(fe.ok);
    EXPECT_EQ(severity_into(findings, "CDC001", "cd_stage2_condition.s0"), "info");
}

TEST(FrontendTest, DividedClockFoldsIntoItsRoot) {
    // q is clocked by a register dividing clk_a: root clk_a, one domain.
    opencdc::frontend::FrontendResult fe;
    auto findings = analyze_fixture("derived_clock.sv", "dc_div", fe);
    ASSERT_TRUE(fe.ok);
    const auto* q = fe.graph.find_node_by_name("dc_div.q");
    ASSERT_NE(q, nullptr);
    EXPECT_EQ(q->root_clock, "clk_a");
    for (const auto& f : findings)
        EXPECT_NE(f.rule_id, "CDC001") << f.reason;
}

TEST(FrontendTest, ClockMuxIsRepresentedByAnInputByDefault) {
    // Default policy: the mux takes the root of one input (no new domain).
    opencdc::frontend::FrontendResult fe;
    analyze_fixture("derived_clock.sv", "dc_mux", fe);
    ASSERT_TRUE(fe.ok);
    const auto* q = fe.graph.find_node_by_name("dc_mux.q");
    ASSERT_NE(q, nullptr);
    EXPECT_NE(q->root_clock, "dc_mux.mclk");
}

TEST(FrontendTest, ClockMuxOfTwoClocksIsItsOwnDomain) {
    // muxed_clock_domain: q's clock is clk_a or clk_b - a domain of its own,
    // so a_r -> q crosses.
    opencdc::frontend::FrontendResult fe;
    opencdc::frontend::FrontendOptions options;
    options.muxed_clock_domain = true;
    auto findings = analyze_fixture("derived_clock.sv", "dc_mux", fe, options);
    ASSERT_TRUE(fe.ok);
    const auto* q = fe.graph.find_node_by_name("dc_mux.q");
    ASSERT_NE(q, nullptr);
    EXPECT_EQ(q->root_clock, "dc_mux.mclk");
    EXPECT_TRUE(q->clock_is_muxed);
    EXPECT_EQ(severity_into(findings, "CDC001", "dc_mux.q"), "error");
}

TEST(FrontendTest, ConcatenationShiftIsASynchronizer) {
    // `{s1, s0} <= {s0, a_r}`: the elements are registers, s0 -> s1 a chain.
    opencdc::frontend::FrontendResult fe;
    auto findings = analyze_fixture("concat_lhs.sv", "cl_sync", fe);
    ASSERT_TRUE(fe.ok);
    for (const char* name : {"cl_sync.s0", "cl_sync.s1"}) {
        const auto* n = fe.graph.find_node_by_name(name);
        ASSERT_NE(n, nullptr) << name;
        EXPECT_EQ(n->kind, NodeKind::Register) << name;
    }
    EXPECT_EQ(severity_into(findings, "CDC001", "cl_sync.s0"), "info");
}

TEST(FrontendTest, ConcatenationLoadedRegistersCross) {
    opencdc::frontend::FrontendResult fe;
    auto findings = analyze_fixture("concat_lhs.sv", "cl_unsync", fe);
    ASSERT_TRUE(fe.ok);
    EXPECT_EQ(severity_into(findings, "CDC001", "cl_unsync.q0"), "error");
    EXPECT_EQ(severity_into(findings, "CDC001", "cl_unsync.q1"), "error");
}

TEST(FrontendTest, ContinuousConcatenationAssignFeedsTheCrossing) {
    // `assign {c, s} = a_r + b_r`: s and c depend on both registers.
    opencdc::frontend::FrontendResult fe;
    auto findings = analyze_fixture("concat_lhs.sv", "cl_assign", fe);
    ASSERT_TRUE(fe.ok);
    bool a = false, b = false;
    for (const auto& f : findings) {
        if (f.rule_id == "CDC001" && f.dest_reg_name == "cl_assign.q") {
            a = a || f.source_reg_name == "cl_assign.a_r";
            b = b || f.source_reg_name == "cl_assign.b_r";
        }
    }
    EXPECT_TRUE(a);
    EXPECT_TRUE(b);
}

TEST(FrontendTest, ResetSynchronizedInAnotherClockDomainIsFlagged) {
    opencdc::frontend::FrontendResult fe;
    auto findings = reset_crossings("rh_wrong_domain", fe);
    ASSERT_TRUE(fe.ok);
    bool found = false;
    for (const auto& f : findings)
        found =
            found || (f.rule_id == "CDC009" && f.source_reg_name == "rh_wrong_domain.rst_sync" &&
                      f.dest_reg_name == "rh_wrong_domain.u_b.q" && f.severity == "error");
    EXPECT_TRUE(found);
}

TEST(FrontendTest, ResetSynchronizedInItsOwnClockDomainIsClean) {
    opencdc::frontend::FrontendResult fe;
    auto findings = reset_crossings("rh_right_domain", fe);
    ASSERT_TRUE(fe.ok);
    for (const auto& f : findings)
        EXPECT_NE(f.rule_id, "CDC009") << f.reason;
}

namespace {
std::vector<cdc_ns::Finding> reconvergence_of(const std::string& top,
                                              opencdc::frontend::FrontendResult& fe,
                                              bool double_sync = false) {
    auto crossings = analyze_fixture("control_dep.sv", top, fe);
    if (!fe.ok)
        return {};
    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);
    cdc_ns::ReconvergenceAnalyzer ra;
    ra.set_report_double_synchronization(double_sync);
    return ra.analyze(fe.graph, dr.domains, crossings);
}
}  // namespace

TEST(FrontendTest, SourceSynchronizedTwiceIsTakenAsSafeByDefault) {
    opencdc::frontend::FrontendResult fe;
    auto reconv = reconvergence_of("cd_sync_twice", fe);
    ASSERT_TRUE(fe.ok);
    EXPECT_TRUE(reconv.empty());
}

TEST(FrontendTest, SourceSynchronizedTwiceReconverges) {
    // report_double_synchronization.
    opencdc::frontend::FrontendResult fe;
    auto reconv = reconvergence_of("cd_sync_twice", fe, true);
    ASSERT_TRUE(fe.ok);
    ASSERT_EQ(reconv.size(), 1u);
    EXPECT_EQ(reconv[0].source_reg_name, "cd_sync_twice.a_r");
}

TEST(FrontendTest, SourceSynchronizedOnceDoesNotReconverge) {
    opencdc::frontend::FrontendResult fe;
    auto reconv = reconvergence_of("cd_sync_once", fe);
    ASSERT_TRUE(fe.ok);
    EXPECT_TRUE(reconv.empty());
}

TEST(FrontendTest, CorrelatedSourcesReconvergeAtAPort) {
    opencdc::frontend::FrontendResult fe;
    auto reconv = reconvergence_of("cd_correlated", fe);
    ASSERT_TRUE(fe.ok);
    ASSERT_EQ(reconv.size(), 1u);
    EXPECT_NE(reconv[0].reason.find("cd_correlated.q"), std::string::npos) << reconv[0].reason;
}

TEST(FrontendTest, IndependentSourcesDoNotReconverge) {
    opencdc::frontend::FrontendResult fe;
    auto reconv = reconvergence_of("cd_independent", fe);
    ASSERT_TRUE(fe.ok);
    EXPECT_TRUE(reconv.empty());
}

TEST(FrontendTest, LogicInFrontOfPackedSynchronizerIsNoChain) {
    opencdc::frontend::FrontendResult fe;
    auto findings = analyze_fixture("control_dep.sv", "cd_packed_logic", fe);
    ASSERT_TRUE(fe.ok);
    EXPECT_EQ(severity_into(findings, "CDC001", "cd_packed_logic.s"), "error");
}

TEST(FrontendTest, SelectIngressOfPackedSynchronizerIsAChain) {
    opencdc::frontend::FrontendResult fe;
    auto findings = analyze_fixture("control_dep.sv", "cd_packed_select", fe);
    ASSERT_TRUE(fe.ok);
    EXPECT_EQ(severity_into(findings, "CDC001", "cd_packed_select.s"), "info");
}
