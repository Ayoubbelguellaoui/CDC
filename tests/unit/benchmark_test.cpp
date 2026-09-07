#include <gtest/gtest.h>

#include <chrono>
#include <random>

#include "analysis/coverage.h"
#include "analysis/signoff.h"
#include "cdc/crossing.h"
#include "clock/domain.h"
#include "ir/graph.h"

using namespace opencdc::ir;
using namespace opencdc::clock;
using namespace opencdc::cdc;

class BenchmarkTest : public ::testing::Test {
   protected:
    Graph graph;
    DomainExtractor domain_extractor;
    CrossingAnalyzer crossing_analyzer;

    void build_graph(size_t num_registers, size_t num_crossings, size_t num_threads) {
        std::mt19937 rng(42);
        std::uniform_int_distribution<int> clk_dist(0, 3);

        for (size_t i = 0; i < num_registers; ++i) {
            int clk_idx = clk_dist(rng);
            std::string clk = "clk_" + std::to_string(clk_idx);
            graph.add_register("mod.reg_" + std::to_string(i), clk, 1,
                               {"bench.sv", static_cast<uint32_t>(i + 1), 5});
        }

        // Create crossings between different clock domains
        const auto& nodes = graph.nodes();
        size_t crossings_added = 0;
        for (size_t i = 0; i < nodes.size() && crossings_added < num_crossings; ++i) {
            if (nodes[i].kind != NodeKind::Register)
                continue;
            for (size_t j = i + 1; j < nodes.size() && crossings_added < num_crossings; ++j) {
                if (nodes[j].kind != NodeKind::Register)
                    continue;
                if (nodes[i].clock_domain != nodes[j].clock_domain) {
                    graph.add_edge(nodes[i].id, nodes[j].id);
                    crossings_added++;
                }
            }
        }
    }
};

TEST_F(BenchmarkTest, CrossingAnalysis100Registers) {
    build_graph(100, 50, 1);

    auto dr = domain_extractor.extract(graph);

    auto start = std::chrono::high_resolution_clock::now();
    auto findings = crossing_analyzer.analyze(graph, dr.domains, dr.register_to_domain, 1);
    auto end = std::chrono::high_resolution_clock::now();

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    EXPECT_LT(ms, 1000);  // Should complete in under 1 second
    EXPECT_FALSE(findings.empty());
}

TEST_F(BenchmarkTest, CrossingAnalysis1000Registers) {
    build_graph(1000, 200, 1);

    auto dr = domain_extractor.extract(graph);

    auto start = std::chrono::high_resolution_clock::now();
    auto findings = crossing_analyzer.analyze(graph, dr.domains, dr.register_to_domain, 1);
    auto end = std::chrono::high_resolution_clock::now();

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    EXPECT_LT(ms, 5000);  // Under 5 seconds
    EXPECT_FALSE(findings.empty());
}

TEST_F(BenchmarkTest, ParallelAnalysis1000Registers) {
    build_graph(1000, 200, 4);

    auto dr = domain_extractor.extract(graph);

    auto start = std::chrono::high_resolution_clock::now();
    auto findings = crossing_analyzer.analyze(graph, dr.domains, dr.register_to_domain, 4);
    auto end = std::chrono::high_resolution_clock::now();

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    EXPECT_LT(ms, 5000);
    EXPECT_FALSE(findings.empty());
}

TEST_F(BenchmarkTest, CoverageComputationLarge) {
    build_graph(500, 100, 1);
    auto dr = domain_extractor.extract(graph);
    auto findings = crossing_analyzer.analyze(graph, dr.domains, dr.register_to_domain, 1);

    auto start = std::chrono::high_resolution_clock::now();
    opencdc::analysis::CoverageEngine ce;
    auto coverage = ce.compute(findings, "complete");
    auto end = std::chrono::high_resolution_clock::now();

    auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    EXPECT_LT(us, 100000);  // Under 100ms
    EXPECT_GT(coverage.counts.total, 0u);
}

TEST_F(BenchmarkTest, SignoffComputationLarge) {
    build_graph(500, 100, 1);
    auto dr = domain_extractor.extract(graph);
    auto findings = crossing_analyzer.analyze(graph, dr.domains, dr.register_to_domain, 1);

    auto start = std::chrono::high_resolution_clock::now();
    opencdc::analysis::SignoffEngine se;
    auto signoff = se.evaluate(findings, "complete");
    auto end = std::chrono::high_resolution_clock::now();

    auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    EXPECT_LT(us, 100000);
    EXPECT_NE(signoff.status, opencdc::analysis::SignoffStatus::Error);
}
