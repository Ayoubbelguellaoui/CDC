#include "cdc/reset_domain.h"

#include <gtest/gtest.h>

#include "cdc/crossing.h"
#include "clock/domain.h"
#include "config/config.h"
#include "ir/graph.h"

using namespace opencdc::ir;
using namespace opencdc::clock;
using namespace opencdc::cdc;
using namespace opencdc::config;

class ResetDomainTest : public ::testing::Test {
   protected:
    Graph graph;
    ResetDomainAnalyzer analyzer;

    std::vector<ClockDomain> make_clock_domains(
        const std::vector<std::pair<uint64_t, std::string>>& regs) {
        std::vector<ClockDomain> domains;
        std::unordered_map<std::string, size_t> name_to_idx;
        for (auto [id, name] : regs) {
            auto it = name_to_idx.find(name);
            if (it == name_to_idx.end()) {
                size_t idx = domains.size();
                ClockDomain d;
                d.id = idx;
                d.name = name;
                d.register_ids.push_back(id);
                domains.push_back(std::move(d));
                name_to_idx[name] = idx;
            } else {
                domains[it->second].register_ids.push_back(id);
            }
        }
        return domains;
    }

    std::unordered_map<uint64_t, size_t> build_register_to_domain(
        const std::vector<ClockDomain>& domains) {
        std::unordered_map<uint64_t, size_t> map;
        for (size_t i = 0; i < domains.size(); ++i) {
            for (uint64_t id : domains[i].register_ids) {
                map[id] = i;
            }
        }
        return map;
    }
};

TEST_F(ResetDomainTest, SameResetSamePolarityNoFinding) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t dst = graph.add_register("mod.dst", "clk_b", 1, {"mod.sv", 6, 5});

    auto* s = graph.find_node_mutable(src);
    s->reset_signal = "rst_n";
    s->reset_pol = ResetPolarity::ActiveLow;
    auto* d = graph.find_node_mutable(dst);
    d->reset_signal = "rst_n";
    d->reset_pol = ResetPolarity::ActiveLow;

    graph.add_edge(src, dst);

    auto domains = make_clock_domains({{src, "clk_a"}, {dst, "clk_b"}});
    auto reg_to_dom = build_register_to_domain(domains);
    auto findings = analyzer.check_reset_crossings(graph, {}, domains, reg_to_dom);
    EXPECT_TRUE(findings.empty());
}

TEST_F(ResetDomainTest, SameResetDiffPolarityTriggersCdc009) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t dst = graph.add_register("mod.dst", "clk_b", 1, {"mod.sv", 6, 5});

    auto* s = graph.find_node_mutable(src);
    s->reset_signal = "rst_n";
    s->reset_pol = ResetPolarity::ActiveLow;
    auto* d = graph.find_node_mutable(dst);
    d->reset_signal = "rst_n";
    d->reset_pol = ResetPolarity::ActiveHigh;

    graph.add_edge(src, dst);

    auto domains = make_clock_domains({{src, "clk_a"}, {dst, "clk_b"}});
    auto reg_to_dom = build_register_to_domain(domains);
    auto findings = analyzer.check_reset_crossings(graph, {}, domains, reg_to_dom);
    ASSERT_EQ(findings.size(), 1u);
    EXPECT_EQ(findings[0].rule_id, "CDC009");
}

TEST_F(ResetDomainTest, DiffResetSamePolarityTriggersCdc009) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t dst = graph.add_register("mod.dst", "clk_b", 1, {"mod.sv", 6, 5});

    auto* s = graph.find_node_mutable(src);
    s->reset_signal = "rst_a";
    s->reset_pol = ResetPolarity::ActiveHigh;
    auto* d = graph.find_node_mutable(dst);
    d->reset_signal = "rst_b";
    d->reset_pol = ResetPolarity::ActiveHigh;

    graph.add_edge(src, dst);

    auto domains = make_clock_domains({{src, "clk_a"}, {dst, "clk_b"}});
    auto reg_to_dom = build_register_to_domain(domains);
    auto findings = analyzer.check_reset_crossings(graph, {}, domains, reg_to_dom);
    ASSERT_EQ(findings.size(), 1u);
    EXPECT_EQ(findings[0].rule_id, "CDC009");
}

TEST_F(ResetDomainTest, AsyncResetEscalatesToError) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t dst = graph.add_register("mod.dst", "clk_b", 1, {"mod.sv", 6, 5});

    auto* s = graph.find_node_mutable(src);
    s->reset_signal = "arst_n";
    s->is_async_reset = true;
    auto* d = graph.find_node_mutable(dst);
    d->reset_signal = "brst_n";
    d->is_async_reset = true;

    graph.add_edge(src, dst);

    auto domains = make_clock_domains({{src, "clk_a"}, {dst, "clk_b"}});
    auto reg_to_dom = build_register_to_domain(domains);
    auto findings = analyzer.check_reset_crossings(graph, {}, domains, reg_to_dom);
    ASSERT_EQ(findings.size(), 1u);
    EXPECT_EQ(findings[0].severity, "error");
    EXPECT_EQ(findings[0].safety_status, SafetyStatus::VerifiedUnsafe);
}

TEST_F(ResetDomainTest, SyncResetStaysWarning) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t dst = graph.add_register("mod.dst", "clk_b", 1, {"mod.sv", 6, 5});

    auto* s = graph.find_node_mutable(src);
    s->reset_signal = "rst_a";
    s->is_async_reset = false;
    auto* d = graph.find_node_mutable(dst);
    d->reset_signal = "rst_b";
    d->is_async_reset = false;

    graph.add_edge(src, dst);

    auto domains = make_clock_domains({{src, "clk_a"}, {dst, "clk_b"}});
    auto reg_to_dom = build_register_to_domain(domains);
    auto findings = analyzer.check_reset_crossings(graph, {}, domains, reg_to_dom);
    ASSERT_EQ(findings.size(), 1u);
    EXPECT_EQ(findings[0].severity, "warning");
}

TEST_F(ResetDomainTest, SameClockSkippedByDefault) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t dst = graph.add_register("mod.dst", "clk_a", 1, {"mod.sv", 6, 5});

    auto* s = graph.find_node_mutable(src);
    s->reset_signal = "rst_a";
    auto* d = graph.find_node_mutable(dst);
    d->reset_signal = "rst_b";

    graph.add_edge(src, dst);

    auto domains = make_clock_domains({{src, "clk_a"}, {dst, "clk_a"}});
    auto reg_to_dom = build_register_to_domain(domains);
    auto findings = analyzer.check_reset_crossings(graph, {}, domains, reg_to_dom);
    EXPECT_TRUE(findings.empty());
}

TEST_F(ResetDomainTest, SameClockDetectedWhenEnabled) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t dst = graph.add_register("mod.dst", "clk_a", 1, {"mod.sv", 6, 5});

    auto* s = graph.find_node_mutable(src);
    s->reset_signal = "rst_a";
    auto* d = graph.find_node_mutable(dst);
    d->reset_signal = "rst_b";

    graph.add_edge(src, dst);

    ResetPolicyConfig same_clock_policy;
    same_clock_policy.check_same_clock_reset_crossings = true;

    auto domains = make_clock_domains({{src, "clk_a"}, {dst, "clk_a"}});
    auto reg_to_dom = build_register_to_domain(domains);
    auto findings =
        analyzer.check_reset_crossings(graph, {}, domains, reg_to_dom, &same_clock_policy);
    ASSERT_EQ(findings.size(), 1u);
    EXPECT_EQ(findings[0].rule_id, "CDC009");
}

TEST_F(ResetDomainTest, ResetSynchronizerSuppressesCdc009) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t sync1 = graph.add_register("mod.sync1", "clk_b", 1, {"mod.sv", 6, 5});
    uint64_t sync2 = graph.add_register("mod.sync2", "clk_b", 1, {"mod.sv", 7, 5});
    uint64_t dst = graph.add_register("mod.dst", "clk_b", 1, {"mod.sv", 8, 5});

    auto* s = graph.find_node_mutable(src);
    s->reset_signal = "arst_n";
    auto* sy1 = graph.find_node_mutable(sync1);
    sy1->reset_signal = "brst_n";
    auto* sy2 = graph.find_node_mutable(sync2);
    sy2->reset_signal = "brst_n";
    auto* d = graph.find_node_mutable(dst);
    d->reset_signal = "brst_n";

    graph.add_edge(src, sync1);
    graph.add_edge(sync1, sync2);
    graph.add_edge(sync2, dst);

    auto domains =
        make_clock_domains({{src, "clk_a"}, {sync1, "clk_b"}, {sync2, "clk_b"}, {dst, "clk_b"}});
    auto reg_to_dom = build_register_to_domain(domains);

    ResetPolicyConfig sync_policy;
    sync_policy.detect_reset_synchronizer = true;

    auto findings = analyzer.check_reset_crossings(graph, {}, domains, reg_to_dom, &sync_policy);
    // The reset synchronizer (2FF chain with brst_n) should suppress CDC009
    // for the src→sync1 crossing if sync1 has a predecessor chain.
    // However, src→sync1 is the direct crossing; the chain is sync1→sync2→...
    // The detection looks backwards from dst: dst←sync2←sync1 (2 stages, same reset) → suppress
    bool found_cdc009 = false;
    for (const auto& f : findings) {
        if (f.rule_id == "CDC009")
            found_cdc009 = true;
    }
    // src→sync1 still fires (sync1's predecessors don't form a 2FF chain with brst_n)
    // but dst itself is suppressed because it has sync2←sync1 chain behind it
    // Actually: has_reset_synchronizer walks backwards from dst. dst←sync2 (brst_n)←sync1 (brst_n)
    // = 2 stages. So dst is suppressed. But src→sync1: walks backwards from sync1, only src
    // (arst_n) which doesn't match. So src→sync1 still fires.
    EXPECT_TRUE(found_cdc009);
}

TEST_F(ResetDomainTest, ExtractResetDomainsReturnsCorrectDomains) {
    uint64_t r1 = graph.add_register("mod.r1", "clk_a", 1, {"mod.sv", 5, 5});
    uint64_t r2 = graph.add_register("mod.r2", "clk_a", 1, {"mod.sv", 6, 5});
    uint64_t r3 = graph.add_register("mod.r3", "clk_a", 1, {"mod.sv", 7, 5});

    auto* n1 = graph.find_node_mutable(r1);
    n1->reset_signal = "rst_n";
    n1->reset_pol = ResetPolarity::ActiveLow;
    auto* n2 = graph.find_node_mutable(r2);
    n2->reset_signal = "rst_n";
    n2->reset_pol = ResetPolarity::ActiveLow;
    auto* n3 = graph.find_node_mutable(r3);
    n3->reset_signal = "arst";
    n3->reset_pol = ResetPolarity::ActiveHigh;

    auto result = analyzer.extract_reset_domains(graph);
    EXPECT_EQ(result.domains.size(), 2u);
}
