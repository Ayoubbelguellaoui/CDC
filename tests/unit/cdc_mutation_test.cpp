#include "cdc/crossing.h"
#include "cdc/cdc006.h"
#include "cdc/synchronizer.h"
#include "cdc/reconvergence.h"
#include "cdc/reset_domain.h"
#include "ir/graph.h"
#include "clock/domain.h"
#include <gtest/gtest.h>

using namespace opencdc::ir;
using namespace opencdc::cdc;
using namespace opencdc::clock;

class CdcMutationTest : public ::testing::Test {
protected:
    Graph graph;
    DomainExtractor domain_extractor;
    CrossingAnalyzer crossing_analyzer;
    Cdc006Analyzer cdc006_analyzer;

    std::vector<ClockDomain> make_domains() {
        std::vector<ClockDomain> d;
        ClockDomain da; da.id = 1; da.name = "clk_a";
        ClockDomain db; db.id = 2; db.name = "clk_b";
        d.push_back(da);
        d.push_back(db);
        return d;
    }

    std::vector<std::string> rule_ids(const std::vector<Finding>& findings) {
        std::vector<std::string> ids;
        for (const auto& f : findings)
            ids.push_back(f.rule_id);
        return ids;
    }
};

TEST_F(CdcMutationTest, GoldenSyncNoFindings) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t meta = graph.add_register("mod.meta", "clk_b", 1, {"mod.sv", 8, 5});
    uint64_t sync = graph.add_register("mod.sync", "clk_b", 1, {"mod.sv", 9, 5});
    uint64_t dst = graph.add_register("mod.dst", "clk_b", 1, {"mod.sv", 10, 5});
    for (auto id : {src, meta, sync, dst}) {
        auto* n = graph.find_node_mutable(id); n->reset_signal = "rst_n";
    }
    graph.add_edge(src, meta);
    graph.add_edge(meta, sync);
    graph.add_edge(sync, dst);

    auto dr = domain_extractor.extract(graph);
    auto findings = crossing_analyzer.analyze(graph, dr.domains, dr.register_to_domain);

    bool has_error = false;
    for (const auto& f : findings) {
        if (f.severity == "error") has_error = true;
    }
    EXPECT_FALSE(has_error) << "Golden sync design should have no errors";
}

TEST_F(CdcMutationTest, RemoveSyncStage2TriggersCdc001) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t meta = graph.add_register("mod.meta", "clk_b", 1, {"mod.sv", 8, 5});
    for (auto id : {src, meta}) {
        auto* n = graph.find_node_mutable(id); n->reset_signal = "rst_n";
    }
    graph.add_edge(src, meta);

    auto dr = domain_extractor.extract(graph);
    auto findings = crossing_analyzer.analyze(graph, dr.domains, dr.register_to_domain);

    bool found_cdc001_error = false;
    for (const auto& f : findings) {
        if (f.rule_id == "CDC001" && f.severity == "error") found_cdc001_error = true;
    }
    EXPECT_TRUE(found_cdc001_error) << "Missing sync stage should trigger CDC001 error";
}

TEST_F(CdcMutationTest, InsertCombBetweenStagesTriggersCdc006) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t meta = graph.add_register("mod.meta", "clk_b", 1, {"mod.sv", 8, 5});
    uint64_t sync = graph.add_register("mod.sync", "clk_b", 1, {"mod.sv", 9, 5});
    uint64_t extra = graph.add_register("mod.extra", "clk_b", 1, {"mod.sv", 10, 5});
    uint64_t comb = graph.add_combinational("mod.and_gate", LogicType::And, {extra, meta}, 1, {"mod.sv", 11, 1});
    for (auto id : {src, meta, sync, extra}) {
        auto* n = graph.find_node_mutable(id); n->reset_signal = "rst_n";
    }
    graph.add_edge(src, meta);
    graph.add_edge(meta, sync);
    graph.add_edge(comb, sync);

    auto findings = cdc006_analyzer.analyze(graph, make_domains(), {});

    bool found_cdc006 = false;
    for (const auto& f : findings) {
        if (f.rule_id == "CDC006") found_cdc006 = true;
    }
    EXPECT_TRUE(found_cdc006) << "Comb between stages should trigger CDC006";
}

TEST_F(CdcMutationTest, MultiBitBusTriggersCdc002) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 8, {"mod.sv", 5, 5});
    uint64_t dst = graph.add_register("mod.dst", "clk_b", 8, {"mod.sv", 6, 5});
    for (auto id : {src, dst}) {
        auto* n = graph.find_node_mutable(id); n->reset_signal = "rst_n";
    }
    graph.add_edge(src, dst);

    auto dr = domain_extractor.extract(graph);
    auto findings = crossing_analyzer.analyze(graph, dr.domains, dr.register_to_domain);

    bool found_cdc002 = false;
    for (const auto& f : findings) {
        if (f.rule_id == "CDC002") found_cdc002 = true;
    }
    EXPECT_TRUE(found_cdc002) << "Multi-bit bus should trigger CDC002";
}

TEST_F(CdcMutationTest, BothNoResetTriggersCdc007) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t dst = graph.add_register("mod.dst", "clk_b", 1, {"mod.sv", 6, 5});
    graph.add_edge(src, dst);

    auto dr = domain_extractor.extract(graph);
    auto findings = crossing_analyzer.analyze(graph, dr.domains, dr.register_to_domain);

    bool found_cdc007 = false;
    for (const auto& f : findings) {
        if (f.rule_id == "CDC007") found_cdc007 = true;
    }
    EXPECT_TRUE(found_cdc007) << "Both registers without reset should trigger CDC007";
}

TEST_F(CdcMutationTest, GatedClockTriggersCdc004) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t dst = graph.add_register("mod.dst", "clk_b", 1, {"mod.sv", 6, 5});
    auto* s = graph.find_node_mutable(src); s->clock_is_gated = true; s->reset_signal = "rst_n";
    auto* d = graph.find_node_mutable(dst); d->reset_signal = "rst_n";
    graph.add_edge(src, dst);

    auto dr = domain_extractor.extract(graph);
    auto findings = crossing_analyzer.analyze(graph, dr.domains, dr.register_to_domain);

    bool found_cdc004 = false;
    for (const auto& f : findings) {
        if (f.rule_id == "CDC004") found_cdc004 = true;
    }
    EXPECT_TRUE(found_cdc004) << "Gated clock should trigger CDC004";
}

TEST_F(CdcMutationTest, ReconvergenceTriggersCdc003) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 2, {"mod.sv", 5, 5});
    uint64_t dst1 = graph.add_register("mod.dst1", "clk_b", 1, {"mod.sv", 8, 5});
    uint64_t dst2 = graph.add_register("mod.dst2", "clk_b", 1, {"mod.sv", 9, 5});
    uint64_t consumer = graph.add_register("mod.consumer", "clk_b", 1, {"mod.sv", 10, 5});
    for (auto id : {src, dst1, dst2, consumer}) {
        auto* n = graph.find_node_mutable(id); n->reset_signal = "rst_n";
    }
    graph.add_edge(src, dst1);
    graph.add_edge(src, dst2);
    graph.add_edge(dst1, consumer);
    graph.add_edge(dst2, consumer);

    auto dr = domain_extractor.extract(graph);
    auto crossings = crossing_analyzer.analyze(graph, dr.domains, dr.register_to_domain);
    ReconvergenceAnalyzer ra;
    auto reconv = ra.analyze(graph, dr.domains, crossings);

    bool found_cdc003 = false;
    for (const auto& f : reconv) {
        if (f.rule_id == "CDC003") found_cdc003 = true;
    }
    EXPECT_TRUE(found_cdc003) << "Multi-bit reconvergence should trigger CDC003";
}

TEST_F(CdcMutationTest, SafetyStatusPopulatedOnAllFindings) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t meta = graph.add_register("mod.meta", "clk_b", 1, {"mod.sv", 8, 5});
    uint64_t sync = graph.add_register("mod.sync", "clk_b", 1, {"mod.sv", 9, 5});
    for (auto id : {src, meta, sync}) {
        auto* n = graph.find_node_mutable(id); n->reset_signal = "rst_n";
    }
    graph.add_edge(src, meta);
    graph.add_edge(meta, sync);

    auto dr = domain_extractor.extract(graph);
    auto findings = crossing_analyzer.analyze(graph, dr.domains, dr.register_to_domain);

    for (const auto& f : findings) {
        EXPECT_NE(f.safety_status, SafetyStatus::Unknown)
            << "Finding " << f.rule_id << " should have explicit safety_status";
        EXPECT_FALSE(f.safety_provenance.empty())
            << "Finding " << f.rule_id << " should have safety_provenance";
    }
}

TEST_F(CdcMutationTest, NamingDoesNotBypassCdc002) {
    uint64_t src = graph.add_register("mod.gray_data", "clk_a", 8, {"mod.sv", 5, 5});
    uint64_t dst = graph.add_register("mod.dst", "clk_b", 8, {"mod.sv", 6, 5});
    for (auto id : {src, dst}) {
        auto* n = graph.find_node_mutable(id); n->reset_signal = "rst_n";
    }
    graph.add_edge(src, dst);

    auto dr = domain_extractor.extract(graph);
    auto findings = crossing_analyzer.analyze(graph, dr.domains, dr.register_to_domain);

    bool found_cdc002 = false;
    for (const auto& f : findings) {
        if (f.rule_id == "CDC002") found_cdc002 = true;
    }
    EXPECT_TRUE(found_cdc002) << "Register named 'gray_data' without structure should still trigger CDC002";
}

TEST_F(CdcMutationTest, MuxedClockNoResetTriggersCdc005) {
    uint64_t src = graph.add_register("mod.src", "clk_muxed", 1, {"mod.sv", 5, 5});
    uint64_t dst = graph.add_register("mod.dst", "clk_b", 1, {"mod.sv", 6, 5});
    auto* s = graph.find_node_mutable(src); s->clock_is_muxed = true;
    graph.add_edge(src, dst);

    auto dr = domain_extractor.extract(graph);
    auto findings = crossing_analyzer.analyze(graph, dr.domains, dr.register_to_domain);

    bool found_cdc005 = false;
    for (const auto& f : findings) {
        if (f.rule_id == "CDC005") found_cdc005 = true;
    }
    EXPECT_TRUE(found_cdc005) << "Muxed clock without reset should trigger CDC005";
}

TEST_F(CdcMutationTest, MultiDomainDaisyChainTriggersCdc008) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t mid1 = graph.add_register("mod.mid1", "clk_b", 1, {"mod.sv", 8, 5});
    uint64_t mid2 = graph.add_register("mod.mid2", "clk_c", 1, {"mod.sv", 9, 5});
    uint64_t dst = graph.add_register("mod.dst", "clk_a", 1, {"mod.sv", 10, 5});
    for (auto id : {src, mid1, mid2, dst}) {
        auto* n = graph.find_node_mutable(id); n->reset_signal = "rst_n";
    }
    graph.add_edge(src, mid1);
    graph.add_edge(mid1, mid2);
    graph.add_edge(mid2, dst);

    auto dr = domain_extractor.extract(graph);
    auto findings = crossing_analyzer.analyze(graph, dr.domains, dr.register_to_domain);

    bool found_cdc008 = false;
    for (const auto& f : findings) {
        if (f.rule_id == "CDC008") found_cdc008 = true;
    }
    EXPECT_TRUE(found_cdc008) << "Register chain crossing 3+ domains should trigger CDC008";
}

TEST_F(CdcMutationTest, ResetDomainCrossingTriggersCdc009) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t dst = graph.add_register("mod.dst", "clk_b", 1, {"mod.sv", 6, 5});
    auto* s = graph.find_node_mutable(src);
    s->reset_signal = "rst_n";
    s->reset_pol = ResetPolarity::ActiveLow;
    auto* d = graph.find_node_mutable(dst);
    d->reset_signal = "rst";
    d->reset_pol = ResetPolarity::ActiveHigh;
    graph.add_edge(src, dst);

    DomainExtractor de;
    auto dr = de.extract(graph);

    opencdc::cdc::ResetDomainAnalyzer rda;
    auto rdomains = rda.extract_reset_domains(graph);
    auto rfindings =
        rda.check_reset_crossings(graph, rdomains.domains, dr.domains, dr.register_to_domain);

    bool found_cdc009 = false;
    for (const auto& f : rfindings) {
        if (f.rule_id == "CDC009") found_cdc009 = true;
    }
    EXPECT_TRUE(found_cdc009) << "Different reset domains should trigger CDC009";
    for (const auto& f : rfindings) {
        if (f.rule_id == "CDC009") {
            EXPECT_NE(f.safety_status, SafetyStatus::Unknown)
                << "CDC009 should have explicit safety_status";
            EXPECT_FALSE(f.safety_provenance.empty())
                << "CDC009 should have safety_provenance";
        }
    }
}

TEST_F(CdcMutationTest, MixedResetTriggersCdc007Info) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t dst = graph.add_register("mod.dst", "clk_b", 1, {"mod.sv", 6, 5});
    auto* s = graph.find_node_mutable(src); s->reset_signal = "rst_n";
    graph.add_edge(src, dst);

    auto dr = domain_extractor.extract(graph);
    auto findings = crossing_analyzer.analyze(graph, dr.domains, dr.register_to_domain);

    bool found_info = false;
    bool found_warning = false;
    for (const auto& f : findings) {
        if (f.rule_id == "CDC007" && f.severity == "info") found_info = true;
        if (f.rule_id == "CDC007" && f.severity == "warning") found_warning = true;
    }
    EXPECT_TRUE(found_info) << "Mixed reset should trigger CDC007 at info severity";
    EXPECT_FALSE(found_warning) << "Mixed reset should NOT trigger CDC007 at warning severity";
}

TEST_F(CdcMutationTest, BothUnresetIsInfoNotWarning) {
    // CDC007 fires as "info" for both-lacking-reset (not "warning") because
    // many datapath registers intentionally omit reset.
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t dst = graph.add_register("mod.dst", "clk_b", 1, {"mod.sv", 6, 5});
    graph.add_edge(src, dst);

    auto dr = domain_extractor.extract(graph);
    auto findings = crossing_analyzer.analyze(graph, dr.domains, dr.register_to_domain);

    bool found_info = false;
    bool found_warning = false;
    for (const auto& f : findings) {
        if (f.rule_id == "CDC007" && f.severity == "info") found_info = true;
        if (f.rule_id == "CDC007" && f.severity == "warning") found_warning = true;
    }
    EXPECT_TRUE(found_info) << "Both-unreset should trigger CDC007 at info severity";
    EXPECT_FALSE(found_warning) << "Both-unreset should NOT trigger CDC007 at warning severity";
}

TEST_F(CdcMutationTest, PathTruncationTriggersCdc010) {
    // Build a binary tree of combinational logic to generate >10000 paths.
    // Source register → combinational tree → leaf registers.
    // With 14 levels, we get 2^14 = 16384 leaf paths (exceeds max_paths=10000).

    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 1, 1});

    constexpr int kTreeLevels = 14;
    std::vector<uint64_t> prev_level = {src};

    for (int level = 0; level < kTreeLevels; ++level) {
        std::vector<uint64_t> curr_level;
        curr_level.reserve(prev_level.size() * 2);

        for (size_t i = 0; i < prev_level.size(); ++i) {
            std::string c1_name = "mod.c_" + std::to_string(level) + "_" + std::to_string(i * 2);
            std::string c2_name = "mod.c_" + std::to_string(level) + "_" + std::to_string(i * 2 + 1);
            uint64_t c1 = graph.add_combinational(c1_name, LogicType::None,
                {prev_level[i]}, 1, {"mod.sv", 100 + level * 1000 + static_cast<uint32_t>(i * 2), 1});
            uint64_t c2 = graph.add_combinational(c2_name, LogicType::None,
                {prev_level[i]}, 1, {"mod.sv", 100 + level * 1000 + static_cast<uint32_t>(i * 2 + 1), 1});
            graph.add_edge(prev_level[i], c1);
            graph.add_edge(prev_level[i], c2);

            if (level == kTreeLevels - 1) {
                std::string r1_name = "mod.leaf_" + std::to_string(i * 2);
                std::string r2_name = "mod.leaf_" + std::to_string(i * 2 + 1);
                uint64_t leaf1 = graph.add_register(r1_name, "clk_b", 1,
                    {"mod.sv", 200 + static_cast<uint32_t>(i * 2), 1});
                uint64_t leaf2 = graph.add_register(r2_name, "clk_b", 1,
                    {"mod.sv", 200 + static_cast<uint32_t>(i * 2 + 1), 1});
                graph.add_edge(c1, leaf1);
                graph.add_edge(c2, leaf2);
            }

            curr_level.push_back(c1);
            curr_level.push_back(c2);
        }

        prev_level = std::move(curr_level);
    }

    auto dr = domain_extractor.extract(graph);
    auto findings = crossing_analyzer.analyze(graph, dr.domains, dr.register_to_domain);

    bool found_cdc010 = false;
    for (const auto& f : findings) {
        if (f.rule_id == "CDC010") {
            found_cdc010 = true;
            EXPECT_EQ(f.safety_status, SafetyStatus::Ambiguous);
            EXPECT_FALSE(f.safety_provenance.empty());
            break;
        }
    }
    EXPECT_TRUE(found_cdc010)
        << "Binary tree exceeding max_paths should trigger CDC010 (path traversal truncated)";
}
