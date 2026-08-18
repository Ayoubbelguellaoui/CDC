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

TEST(FrontendTest, SameDomainHasOneRegister) {
    opencdc::frontend::SlangAdapter adapter;
    auto result = adapter.elaborate(
        {fixture_path("same_domain.sv")}, "simple_same_domain");
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.graph.register_count(), 1u);
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

    cdc_ns::CrossingAnalyzer ca;
    auto findings = ca.analyze(fe.graph, dr.domains);

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
    auto findings = ca.analyze(fe.graph, dr.domains);

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
    auto findings = ca.analyze(fe.graph, dr.domains);

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
    auto findings = ca.analyze(fe.graph, dr.domains);

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
    auto findings = ca.analyze(fe.graph, dr.domains);

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
    auto findings = ca.analyze(fe.graph, dr.domains);

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
    auto findings = ca.analyze(fe.graph, dr.domains);

    bool has_error = false;
    for (const auto& f : findings) {
        if (f.severity == "error") has_error = true;
    }
    EXPECT_TRUE(has_error);
}

TEST(FrontendTest, GatedClockNoFalseCrossing) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate(
        {fixture_path("gated_clock.sv")}, "gated_clock");
    ASSERT_TRUE(fe.ok);

    bool has_gated = false;
    for (const auto& node : fe.graph.nodes()) {
        if (node.kind == NodeKind::Register && node.clock_is_gated) {
            has_gated = true;
            EXPECT_EQ(node.root_clock, "clk");
        }
    }
    EXPECT_TRUE(has_gated);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);

    cdc_ns::CrossingAnalyzer ca;
    auto findings = ca.analyze(fe.graph, dr.domains);
    EXPECT_TRUE(findings.empty());
}

TEST(FrontendTest, GatedCrossingDetected) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate(
        {fixture_path("gated_crossing.sv")}, "gated_crossing");
    ASSERT_TRUE(fe.ok);

    cdc_clock::DomainExtractor de;
    auto dr = de.extract(fe.graph);

    cdc_ns::CrossingAnalyzer ca;
    auto findings = ca.analyze(fe.graph, dr.domains);

    ASSERT_EQ(findings.size(), 1u);
    EXPECT_EQ(findings[0].rule_id, "CDC001");
    EXPECT_EQ(findings[0].source_domain, "clk_a");
    EXPECT_EQ(findings[0].dest_domain, "clk_b");
}

TEST(FrontendTest, GatedClockResolvedInVerbose) {
    opencdc::frontend::SlangAdapter adapter;
    auto fe = adapter.elaborate(
        {fixture_path("gated_clock.sv")}, "gated_clock");
    ASSERT_TRUE(fe.ok);

    bool found_root = false;
    for (const auto& node : fe.graph.nodes()) {
        if (node.kind == NodeKind::Register) {
            EXPECT_EQ(node.root_clock, "clk");
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
    auto findings = ca.analyze(fe.graph, dr.domains);
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
    auto findings = ca.analyze(fe.graph, dr.domains);

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
    auto findings = ca.analyze(fe.graph, dr.domains);

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
    auto findings = ca.analyze(fe.graph, dr.domains);

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
    auto findings = ca.analyze(fe.graph, dr.domains);
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
    auto findings = ca.analyze(fe.graph, dr.domains);
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
    auto findings = ca.analyze(fe.graph, dr.domains);
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
    auto findings = ca.analyze(fe.graph, dr.domains);

    opencdc::report::Reporter reporter;
    std::ostringstream os;
    reporter.report_json(findings, os);

    std::string out = os.str();
    EXPECT_EQ(out.front(), '[');
    EXPECT_NE(out.find("CDC001"), std::string::npos);
    EXPECT_NE(out.find("simple_cdc_crossing.src_ff"), std::string::npos);
}
