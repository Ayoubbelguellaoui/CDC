#include "cdc/crossing.h"
#include "ir/graph.h"
#include "clock/domain.h"
#include <gtest/gtest.h>
#include <algorithm>

using namespace opencdc::ir;
using namespace opencdc::cdc;
using namespace opencdc::clock;

class ThreadDeterminismTest : public ::testing::Test {
protected:
    void build_graph() {
        for (int i = 0; i < 20; ++i) {
            uint64_t src = graph.add_register("mod.src_" + std::to_string(i), "clk_a", 1,
                                               {"mod.sv", 5, 5});
            uint64_t dst = graph.add_register("mod.dst_" + std::to_string(i), "clk_b", 1,
                                               {"mod.sv", 6, 5});
            auto* s = graph.find_node_mutable(src);
            s->reset_signal = "rst_n";
            auto* d = graph.find_node_mutable(dst);
            d->reset_signal = "rst_n";
            graph.add_edge(src, dst);
        }
    }

    std::vector<std::string> finding_keys(const std::vector<Finding>& findings) {
        std::vector<std::string> keys;
        for (const auto& f : findings) {
            keys.push_back(f.rule_id + ":" + f.source_reg_name + "->" + f.dest_reg_name);
        }
        std::sort(keys.begin(), keys.end());
        return keys;
    }

    Graph graph;
};

TEST_F(ThreadDeterminismTest, ThreadCountDoesNotAffectFindings) {
    build_graph();
    DomainExtractor domain_extractor;
    auto dr = domain_extractor.extract(graph);

    auto run_analysis = [&](size_t threads) {
        Graph g = graph;
        CrossingAnalyzer ca;
        return ca.analyze(g, dr.domains, dr.register_to_domain, threads);
    };

    auto f1 = run_analysis(1);
    auto f2 = run_analysis(2);
    auto f4 = run_analysis(4);

    auto k1 = finding_keys(f1);
    auto k2 = finding_keys(f2);
    auto k4 = finding_keys(f4);

    EXPECT_EQ(k1, k2) << "Findings differ between 1 and 2 threads";
    EXPECT_EQ(k2, k4) << "Findings differ between 2 and 4 threads";
}

TEST_F(ThreadDeterminismTest, ComplexTopologyIdentical) {
    uint64_t src = graph.add_register("mod.src", "clk_a", 2, {"mod.sv", 5, 5});
    uint64_t dst1 = graph.add_register("mod.dst1", "clk_b", 1, {"mod.sv", 8, 5});
    uint64_t dst2 = graph.add_register("mod.dst2", "clk_b", 1, {"mod.sv", 9, 5});
    uint64_t consumer = graph.add_register("mod.consumer", "clk_b", 1, {"mod.sv", 10, 5});
    uint64_t sync_meta = graph.add_register("mod.sync_meta", "clk_b", 1, {"mod.sv", 11, 5});
    uint64_t sync_out = graph.add_register("mod.sync_out", "clk_b", 1, {"mod.sv", 12, 5});

    for (uint64_t id : {src, dst1, dst2, consumer, sync_meta, sync_out}) {
        auto* n = graph.find_node_mutable(id);
        n->reset_signal = "rst_n";
    }

    graph.add_edge(src, dst1);
    graph.add_edge(src, dst2);
    graph.add_edge(dst1, consumer);
    graph.add_edge(dst2, consumer);
    graph.add_edge(src, sync_meta);
    graph.add_edge(sync_meta, sync_out);

    DomainExtractor domain_extractor;
    auto dr = domain_extractor.extract(graph);

    auto run_analysis = [&](size_t threads) {
        Graph g = graph;
        CrossingAnalyzer ca;
        return ca.analyze(g, dr.domains, dr.register_to_domain, threads);
    };

    auto f1 = run_analysis(1);
    auto f2 = run_analysis(2);
    auto f4 = run_analysis(4);

    auto k1 = finding_keys(f1);
    auto k2 = finding_keys(f2);
    auto k4 = finding_keys(f4);

    EXPECT_EQ(k1, k2) << "Complex topology differs between 1 and 2 threads";
    EXPECT_EQ(k2, k4) << "Complex topology differs between 2 and 4 threads";
}
