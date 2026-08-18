#include <gtest/gtest.h>
#include "clock/domain.h"
#include "ir/graph.h"

using namespace opencdc::ir;
using namespace opencdc::clock;

TEST(DomainTest, SingleDomain) {
    Graph g;
    g.add_register("mod.ff1", "clk", 1, {});
    g.add_register("mod.ff2", "clk", 1, {});

    DomainExtractor extractor;
    auto result = extractor.extract(g);

    ASSERT_EQ(result.domains.size(), 1u);
    EXPECT_EQ(result.domains[0].name, "clk");
    EXPECT_EQ(result.domains[0].register_ids.size(), 2u);
    EXPECT_TRUE(result.warnings.empty());
}

TEST(DomainTest, MultipleDomains) {
    Graph g;
    g.add_register("mod.ff1", "clk_a", 1, {});
    g.add_register("mod.ff2", "clk_b", 1, {});
    g.add_register("mod.ff3", "clk_a", 1, {});

    DomainExtractor extractor;
    auto result = extractor.extract(g);

    ASSERT_EQ(result.domains.size(), 2u);

    const ClockDomain* dom_a = nullptr;
    const ClockDomain* dom_b = nullptr;
    for (const auto& d : result.domains) {
        if (d.name == "clk_a") dom_a = &d;
        if (d.name == "clk_b") dom_b = &d;
    }
    ASSERT_NE(dom_a, nullptr);
    ASSERT_NE(dom_b, nullptr);
    EXPECT_EQ(dom_a->register_ids.size(), 2u);
    EXPECT_EQ(dom_b->register_ids.size(), 1u);
}

TEST(DomainTest, UnknownClockEmitsWarning) {
    Graph g;
    g.add_register("mod.ff1", "unknown", 1, {});

    DomainExtractor extractor;
    auto result = extractor.extract(g);

    EXPECT_TRUE(result.domains.empty());
    ASSERT_EQ(result.warnings.size(), 1u);
    EXPECT_NE(result.warnings[0].find("unresolved"), std::string::npos);
}

TEST(DomainTest, SameDomainCheck) {
    Graph g;
    uint64_t r1 = g.add_register("r1", "clk_a", 1, {});
    uint64_t r2 = g.add_register("r2", "clk_a", 1, {});
    uint64_t r3 = g.add_register("r3", "clk_b", 1, {});

    DomainExtractor extractor;
    auto result = extractor.extract(g);

    EXPECT_TRUE(extractor.same_domain(r1, r2, result.domains));
    EXPECT_FALSE(extractor.same_domain(r1, r3, result.domains));
}

TEST(DomainTest, FindDomainForRegister) {
    Graph g;
    uint64_t r1 = g.add_register("r1", "clk_x", 1, {});
    uint64_t r2 = g.add_register("r2", "clk_y", 1, {});

    DomainExtractor extractor;
    auto result = extractor.extract(g);

    const ClockDomain* dom1 = extractor.find_domain(r1, result.domains);
    const ClockDomain* dom2 = extractor.find_domain(r2, result.domains);
    ASSERT_NE(dom1, nullptr);
    ASSERT_NE(dom2, nullptr);
    EXPECT_EQ(dom1->name, "clk_x");
    EXPECT_EQ(dom2->name, "clk_y");
}

TEST(DomainTest, ThreeDomains) {
    Graph g;
    g.add_register("r1", "clk_a", 1, {});
    g.add_register("r2", "clk_b", 1, {});
    g.add_register("r3", "clk_c", 1, {});

    DomainExtractor extractor;
    auto result = extractor.extract(g);

    EXPECT_EQ(result.domains.size(), 3u);
}
