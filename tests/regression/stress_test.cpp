#include <gtest/gtest.h>
#include "frontend/slang_adapter.h"
#include "clock/domain.h"
#include "cdc/crossing.h"
#include "cdc/reconvergence.h"
#include "cdc/cdc006.h"
#include "cdc/waiver.h"
#include "rules/rule.h"
#include "report/report.h"
#include <string>

namespace cdc_clock = opencdc::clock;
namespace cdc_ns = opencdc::cdc;

static std::string fixture_sv(const std::string& name) {
    return std::string(FIXTURES_DIR) + "/sv/" + name;
}

class StressTest : public ::testing::Test {
protected:
    void SetUp() override {
        opencdc::frontend::SlangAdapter adapter;
        fe = adapter.elaborate(
            {fixture_sv("stress_test_system.sv")}, "stress_test_system");
        ASSERT_TRUE(fe.ok) << "Parse failed";

        cdc_clock::DomainExtractor de;
        dr = de.extract(fe.graph);
        ASSERT_FALSE(dr.domains.empty()) << "Domain resolution failed";

        cdc_ns::PatternRecognizer pattern_recognizer;
        pattern_recognizer.analyze_and_annotate(fe.graph);
        cdc_ns::CrossingAnalyzer ca;
        ca.set_pattern_recognizer(&pattern_recognizer);
        findings = ca.analyze(fe.graph, dr.domains, dr.register_to_domain);

        cdc_ns::ReconvergenceAnalyzer ra;
        auto recon_findings = ra.analyze(fe.graph, dr.domains, findings);
        for (auto& f : recon_findings) {
            findings.push_back(std::move(f));
        }

        cdc_ns::Cdc006Analyzer cdc006;
        auto cdc006_findings = cdc006.analyze(fe.graph, dr.domains, dr.register_to_domain);
        for (auto& f : cdc006_findings) {
            findings.push_back(std::move(f));
        }
    }

    int count_by_rule(const std::string& rule) const {
        int count = 0;
        for (const auto& f : findings) {
            if (f.rule_id == rule) count++;
        }
        return count;
    }

    bool has_finding(const std::string& rule, const std::string& source_substr) const {
        for (const auto& f : findings) {
            if (f.rule_id == rule && f.source_reg_name.find(source_substr) != std::string::npos)
                return true;
        }
        return false;
    }

    opencdc::frontend::FrontendResult fe;
    cdc_clock::DomainResult dr;
    std::vector<cdc_ns::Finding> findings;
};

TEST_F(StressTest, TotalFindingCount) {
    EXPECT_EQ(findings.size(), 37u);
}

TEST_F(StressTest, Cdc001_UnsynchronizedCrossing) {
    EXPECT_EQ(count_by_rule("CDC001"), 16);
    EXPECT_TRUE(has_finding("CDC001", "u_sync_2ff_bus.src_reg"));
}

TEST_F(StressTest, Cdc002_MultiBitCrossing) {
    // Includes synchronized multi-bit crossings (e.g. u_sync_2ff_bus.src_reg,
    // 8 bits through a 2FF chain): per-bit synchronization of a bus is unsafe,
    // so CDC002 fires even when a sync chain is present.
    EXPECT_EQ(count_by_rule("CDC002"), 9);
}

TEST_F(StressTest, Cdc003_ReconvergenceDetected) {
    EXPECT_EQ(count_by_rule("CDC003"), 1);
    EXPECT_TRUE(has_finding("CDC003", "u_reconvergence.src_ff"));
}

TEST_F(StressTest, Cdc004_GatedClockDetected) {
    EXPECT_EQ(count_by_rule("CDC004"), 2);
}

TEST_F(StressTest, Cdc005_MuxedClockDetected) {
    EXPECT_EQ(count_by_rule("CDC005"), 2);
}

TEST_F(StressTest, Cdc006_NoDirectFeedFalsePositive) {
    EXPECT_EQ(count_by_rule("CDC006"), 0);
}

TEST_F(StressTest, Cdc007_MissingReset) {
    // Fires when either side of the crossing lacks a reset.
    EXPECT_EQ(count_by_rule("CDC007"), 3);
}

TEST_F(StressTest, Cdc008_MultiDomainDaisyChain) {
    EXPECT_EQ(count_by_rule("CDC008"), 4);
}

TEST_F(StressTest, GrayCounterSuppressed) {
    for (const auto& f : findings) {
        if (f.source_reg_name.find("gray_counter") != std::string::npos) {
            EXPECT_NE(f.rule_id, "CDC002")
                << "gray_counter should not trigger CDC002, but got finding: " << f.reason;
        }
    }
}

TEST_F(StressTest, WaiverConfigSuppresses) {
    cdc_ns::WaiverEngine engine;
    cdc_ns::Waiver waiver;
    waiver.rule_id = "CDC001";
    waiver.source_reg_name = "u_sync_2ff_bus.src_reg";
    waiver.justification = "Async FIFO handles this crossing";
    waiver.owner = "@rtl-team";
    engine.add_waiver(waiver);

    auto remaining = engine.apply(findings);
    int waived_count = 0;
    for (const auto& f : remaining) {
        if (f.waived && f.rule_id == "CDC001") waived_count++;
    }
    EXPECT_EQ(waived_count, 1);
}

TEST_F(StressTest, AllClockDomainsDetected) {
    EXPECT_EQ(dr.domains.size(), 4u);
}

TEST_F(StressTest, AllRulesTriggered) {
    std::set<std::string> expected = {"CDC001", "CDC002", "CDC003", "CDC004", "CDC005", "CDC007", "CDC008"};
    std::set<std::string> actual;
    for (const auto& f : findings) {
        actual.insert(f.rule_id);
    }
    for (const auto& rule : expected) {
        EXPECT_TRUE(actual.count(rule)) << rule << " not triggered";
    }
}
