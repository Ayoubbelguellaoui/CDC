#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "analysis/analyzer.h"
#include "frontend/frontend.h"

namespace opencdc::analysis {
namespace {

using frontend::InputType;

std::string fx(const char* rel) {
    return std::string(FIXTURES_DIR) + "/netlist/" + rel;
}

const cdc::Finding* find_rule(const std::vector<cdc::Finding>& findings, const std::string& id) {
    for (const auto& f : findings) {
        if (f.rule_id == id)
            return &f;
    }
    return nullptr;
}

TEST(NetlistFrontendTest, DetectsNetlistInput) {
    std::vector<std::string> files = {fx("cdc_crossing.v")};
    EXPECT_EQ(frontend::detect_input_type(files), InputType::Netlist);
    files = {fx("sync_2ff.v")};
    EXPECT_EQ(frontend::detect_input_type(files), InputType::Netlist);
    files = {fx("gated_clock.v")};
    EXPECT_EQ(frontend::detect_input_type(files), InputType::Netlist);
}

TEST(NetlistFrontendTest, DetectsRtlInput) {
    // A behavioral RTL fixture must be detected as rtl (preserve default).
    std::vector<std::string> files = {std::string(FIXTURES_DIR) + "/sv/gated_clock.sv"};
    EXPECT_EQ(frontend::detect_input_type(files), InputType::Rtl);
}

TEST(NetlistFrontendTest, UnsyncCrossingReportsCdc001Error) {
    AnalysisRequest req;
    req.input_files = {fx("cdc_crossing.v")};
    req.top_module = "cdc_crossing";
    req.input_type = InputType::Netlist;
    Analyzer analyzer;
    auto result = analyzer.run(req);
    ASSERT_TRUE(result.ok) << (result.errors.empty() ? "" : result.errors.front());
    EXPECT_EQ(result.input_type, "netlist");

    const cdc::Finding* f = find_rule(result.findings, "CDC001");
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(f->severity, "error");
    EXPECT_EQ(f->source_domain, "clk_a");
    EXPECT_EQ(f->dest_domain, "clk_b");
}

TEST(NetlistFrontendTest, LowPowerIsolationDowngradesCdc001Only) {
    AnalysisRequest req;
    req.input_files = {fx("low_power.v")};
    req.top_module = "low_power";
    req.input_type = InputType::Netlist;
    Analyzer analyzer;
    auto result = analyzer.run(req);
    ASSERT_TRUE(result.ok) << (result.errors.empty() ? "" : result.errors.front());

    // Retention flops are real registers: the crossing exists at all, and
    // the bit flops regroup into a width-2 bus (CDC002 bus_width below).
    // Modeling them as Unknown (opaque combinational) would hide it.
    const cdc::Finding* f = find_rule(result.findings, "CDC001");
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(f->source_reg_name, "src_reg");
    EXPECT_EQ(f->dest_reg_name, "dst_reg");
    // CDC001 alone is downgraded: the path crosses a low-power isolation
    // cell (methodology-managed power-domain boundary, not a synchronizer).
    EXPECT_EQ(f->severity, "info");
    EXPECT_EQ(f->safety_status, cdc::SafetyStatus::VerifiedSafe);
    EXPECT_NE(f->safety_provenance.find("isolation"), std::string::npos);

    // Multi-bit rules are independent of synchronization and stay errors.
    const cdc::Finding* mb = find_rule(result.findings, "CDC002");
    ASSERT_NE(mb, nullptr);
    EXPECT_EQ(mb->severity, "error");
    EXPECT_EQ(mb->bus_width, 2u);
    EXPECT_EQ(mb->dest_reg_name, "dst_reg");

    // Both low-power cell types are recognized: no unmapped-cell warning.
    for (const auto& w : result.warnings) {
        EXPECT_EQ(w.find("unmapped"), std::string::npos) << w;
    }
}

TEST(NetlistFrontendTest, TwoFfsyncIsStructurallySafe) {
    AnalysisRequest req;
    req.input_files = {fx("sync_2ff.v")};
    req.top_module = "sync_2ff";
    req.input_type = InputType::Netlist;
    Analyzer analyzer;
    auto result = analyzer.run(req);
    ASSERT_TRUE(result.ok);

    const cdc::Finding* f = find_rule(result.findings, "CDC001");
    ASSERT_NE(f, nullptr);
    // Structural 2FF chain detected at the destination -> safe/info.
    EXPECT_EQ(f->severity, "info");
    EXPECT_EQ(f->safety_status, cdc::SafetyStatus::VerifiedSafe);
}

TEST(NetlistFrontendTest, GatedClockCrossingReportsCdc004) {
    AnalysisRequest req;
    req.input_files = {fx("gated_crossing.v")};
    req.top_module = "gated_crossing";
    req.input_type = InputType::Netlist;
    Analyzer analyzer;
    auto result = analyzer.run(req);
    ASSERT_TRUE(result.ok) << (result.errors.empty() ? "" : result.errors.front());

    const cdc::Finding* f = find_rule(result.findings, "CDC004");
    ASSERT_NE(f, nullptr) << "expected gated-clock crossing finding";
    EXPECT_EQ(f->severity, "warning");
}

TEST(NetlistFrontendTest, BitBlastedBusHasNoCrossBitCrossings) {
    // A 4-bit bus crossing bit-blasted into per-bit flops must yield exactly
    // 4 crossings (u[i] -> v[i]), NOT 16. Guards against collapsing `bus[i]`
    // into one shared net node, which fabricates cross-bit paths.
    AnalysisRequest req;
    req.input_files = {fx("bus_crossing.v")};
    req.top_module = "bus_crossing";
    req.input_type = InputType::Netlist;
    Analyzer analyzer;
    auto result = analyzer.run(req);
    ASSERT_TRUE(result.ok) << (result.errors.empty() ? "" : result.errors.front());

    int crossings = 0;
    for (const auto& f : result.findings) {
        if (f.rule_id != "CDC001")
            continue;
        ++crossings;
        // u<i> -> v<i>: the trailing bit index must match (no cross-bit paths).
        ASSERT_FALSE(f.source_reg_name.empty());
        ASSERT_FALSE(f.dest_reg_name.empty());
        EXPECT_EQ(f.source_reg_name.back(), f.dest_reg_name.back())
            << "cross-bit crossing: " << f.source_reg_name << " -> " << f.dest_reg_name;
        EXPECT_EQ(f.source_reg_name.front(), 'u');
        EXPECT_EQ(f.dest_reg_name.front(), 'v');
    }
    EXPECT_EQ(crossings, 4);
}

TEST(NetlistFrontendTest, AutoDetectionEndToEnd) {
    // InputType::Auto should classify the structural netlist and analyze it.
    AnalysisRequest req;
    req.input_files = {fx("cdc_crossing.v")};
    req.top_module = "cdc_crossing";
    req.input_type = InputType::Auto;
    Analyzer analyzer;
    auto result = analyzer.run(req);
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.input_type, "netlist");
    EXPECT_NE(find_rule(result.findings, "CDC001"), nullptr);
}

TEST(NetlistFrontendTest, CellMapClassifiesResetPolarity) {
    // With a cell map classifying DFFCNQ_* as dff_reset (active-low async),
    // a reset net should be recorded on the register (drives CDC009/etc).
    AnalysisRequest req;
    req.input_files = {fx("reset_flop.v")};
    req.top_module = "reset_flop";
    req.input_type = InputType::Netlist;
    req.cell_map_path = fx("cells.yaml");
    Analyzer analyzer;
    auto result = analyzer.run(req);
    ASSERT_TRUE(result.ok) << (result.errors.empty() ? "" : result.errors.front());
    // Find a register node and check it recorded the reset net.
    bool found_reset = false;
    for (const auto& n : result.graph.nodes()) {
        if (n.kind == ir::NodeKind::Register && !n.reset_signal.empty()) {
            found_reset = true;
            EXPECT_EQ(n.reset_signal, "rst_n");
            EXPECT_EQ(n.reset_pol, ir::ResetPolarity::ActiveLow);
            EXPECT_TRUE(n.is_async_reset);
        }
    }
    EXPECT_TRUE(found_reset);
}

size_t count_rule(const std::vector<cdc::Finding>& findings, const std::string& id) {
    size_t n = 0;
    for (const auto& f : findings) {
        if (f.rule_id == id)
            ++n;
    }
    return n;
}

TEST(NetlistFrontendTest, BusRegroupMergesBitFlopsIntoWideRegister) {
    // yosys bit-blasts `src_reg`/`dst_reg` into per-bit flops `name[i]`
    // (escaped ids). Bus regrouping must re-merge each group into ONE wide
    // register, so the crossing is reported exactly as the RTL analysis
    // reports it: a single CDC001 + CDC002 pair at bus width 4 — not four
    // narrow CDC001 findings.
    AnalysisRequest req;
    req.input_files = {fx("bus_regroup.v")};
    req.top_module = "bus_regroup";
    req.input_type = InputType::Netlist;
    Analyzer analyzer;
    auto result = analyzer.run(req);
    ASSERT_TRUE(result.ok) << (result.errors.empty() ? "" : result.errors.front());

    EXPECT_EQ(count_rule(result.findings, "CDC001"), 1u);
    EXPECT_EQ(count_rule(result.findings, "CDC002"), 1u);
    const cdc::Finding* f = find_rule(result.findings, "CDC001");
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(f->source_reg_name, "src_reg_reg");
    EXPECT_EQ(f->dest_reg_name, "dst_reg_reg");
    const cdc::Finding* mb = find_rule(result.findings, "CDC002");
    ASSERT_NE(mb, nullptr);
    EXPECT_EQ(mb->bus_width, 4u);
}

TEST(NetlistFrontendTest, GrayRecoverySuppressesCdc002LikeRtlAnalysis) {
    // `gray_src_reg[i] = d[i] ^ d[i+1]` (adjacent-bit XOR of one bus, with
    // the folded boundary bit fed directly) is the synthesized shape of the
    // RTL gray encoder. Structural gray recovery must re-annotate it so the
    // netlist reaches the same verdict as RTL: CDC001 for the (safe-per-bit)
    // crossing, NO CDC002 — while the plain-copy crossing in bus_regroup.v
    // still fires CDC002 (negative control).
    AnalysisRequest req;
    req.input_files = {fx("gray_regroup.v")};
    req.top_module = "gray_regroup";
    req.input_type = InputType::Netlist;
    Analyzer analyzer;
    auto result = analyzer.run(req);
    ASSERT_TRUE(result.ok) << (result.errors.empty() ? "" : result.errors.front());

    EXPECT_EQ(count_rule(result.findings, "CDC001"), 1u);
    EXPECT_EQ(count_rule(result.findings, "CDC002"), 0u);
    const cdc::Finding* f = find_rule(result.findings, "CDC001");
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(f->source_reg_name, "gray_src_reg_reg");
    EXPECT_EQ(f->dest_reg_name, "dst_reg_reg");
}

TEST(NetlistFrontendTest, NameMapRenamesNetlistRegistersToRtlNames) {
    // Synthesis renamed `data_reg`/`dst_reg` into `u_data/ff_reg[i]`/
    // `u_dst/ff_reg[i]`. The name map restores the RTL names before bus
    // regrouping, so findings (and RTL-authored waivers keyed on them) see
    // the RTL register names at RTL granularity.
    AnalysisRequest req;
    req.input_files = {fx("namemap_crossing.v")};
    req.top_module = "namemap_crossing";
    req.input_type = InputType::Netlist;
    req.name_map_path = fx("namemap_crossing.map");
    Analyzer analyzer;
    auto result = analyzer.run(req);
    ASSERT_TRUE(result.ok) << (result.errors.empty() ? "" : result.errors.front());

    EXPECT_EQ(count_rule(result.findings, "CDC001"), 1u);
    const cdc::Finding* f = find_rule(result.findings, "CDC001");
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(f->source_reg_name, "data_reg");
    EXPECT_EQ(f->dest_reg_name, "dst_reg");
    // The mapped bit group regroups under the RTL name at full bus width.
    EXPECT_EQ(count_rule(result.findings, "CDC002"), 1u);
    const cdc::Finding* mb = find_rule(result.findings, "CDC002");
    ASSERT_NE(mb, nullptr);
    EXPECT_EQ(mb->bus_width, 2u);
    // A stale map entry surfaces as a warning instead of silently passing.
    bool warned = false;
    for (const auto& w : result.warnings) {
        if (w.find("ghost_reg") != std::string::npos)
            warned = true;
    }
    EXPECT_TRUE(warned);
}

TEST(NetlistFrontendTest, HierarchicalInstancesFlattenIntoPrefixedNames) {
    // The crossing lives inside a submodule instantiated on two different
    // clocks; elaboration must flatten it to prefixed register names so the
    // analyzer sees it exactly like a flat netlist.
    AnalysisRequest req;
    req.input_files = {fx("hier_crossing.v")};
    req.top_module = "hier_crossing";
    req.input_type = InputType::Netlist;
    Analyzer analyzer;
    auto result = analyzer.run(req);
    ASSERT_TRUE(result.ok) << (result.errors.empty() ? "" : result.errors.front());

    EXPECT_EQ(count_rule(result.findings, "CDC001"), 1u);
    const cdc::Finding* f = find_rule(result.findings, "CDC001");
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(f->source_reg_name, "u_src.ff0");
    EXPECT_EQ(f->dest_reg_name, "u_dst.ff0");
    EXPECT_EQ(f->source_domain, "clk_a");
    EXPECT_EQ(f->dest_domain, "clk_b");
}

TEST(NetlistFrontendTest, RecursiveModuleInstantiationIsRejected) {
    // hier_a instantiates hier_b which instantiates hier_a: elaboration must
    // fail with a clear error instead of recursing or silently dropping
    // logic.
    AnalysisRequest req;
    req.input_files = {fx("hier_recursive.v")};
    req.top_module = "hier_a";
    req.input_type = InputType::Netlist;
    Analyzer analyzer;
    auto result = analyzer.run(req);
    ASSERT_FALSE(result.ok);
    bool found_recursive = false;
    for (const auto& e : result.errors) {
        if (e.find("recursive module instantiation") != std::string::npos)
            found_recursive = true;
    }
    EXPECT_TRUE(found_recursive);
}

TEST(NetlistFrontendTest, XorCellClassifiesAsXorNotOr) {
    // Regression for pattern-name logic-type inference: "XOR"/"XNOR" contain
    // "OR" (and "NAND" contains "AND") as substrings, so match order decides.
    // A cell named `XOR2_X1` must classify as LogicType::Xor — structural
    // gray recognition depends on it.
    auto fe = frontend::make_netlist_adapter();
    ASSERT_NE(fe, nullptr);
    frontend::FrontendOptions opts;
    frontend::FrontendResult r = fe->elaborate({fx("xor_cells.v")}, "xor_cells", opts);
    ASSERT_TRUE(r.ok) << (r.errors.empty() ? "" : r.errors.front());

    const ir::Node* g = r.graph.find_node_by_name("g");
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(g->logic_type, ir::LogicType::Xor);
    const ir::Node* hn = r.graph.find_node_by_name("hn");
    ASSERT_NE(hn, nullptr);
    EXPECT_EQ(hn->logic_type, ir::LogicType::And);
}

TEST(NetlistFrontendTest, MuxedClockResolvesToDefaultPathRoot) {
    // `$_MUX_ m0 (.A(clk_b), .B(clk_a), .S(clk_sel), .Y(selected_clk))` —
    // the muxed clock's representative root must be the default-path
    // (select = 0) input A = clk_b, matching the RTL frontend's resolution
    // of `sel ? clk_a : clk_b`. Before mux input normalization the resolver
    // picked the LAST connection (the select pin), yielding a bogus
    // (clk_a -> clk_sel) domain pair instead of (clk_a -> clk_b).
    AnalysisRequest req;
    req.input_files = {fx("mux_clock.v")};
    req.top_module = "mux_clock";
    req.input_type = InputType::Netlist;
    Analyzer analyzer;
    auto result = analyzer.run(req);
    ASSERT_TRUE(result.ok) << (result.errors.empty() ? "" : result.errors.front());

    const cdc::Finding* f = find_rule(result.findings, "CDC001");
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(f->source_domain, "clk_a");
    EXPECT_EQ(f->dest_domain, "clk_b");
}

TEST(NetlistFrontendTest, MuxedClockIsItsOwnDomainUnderSignoffPolicy) {
    // With muxed_clock_domain the mux of clk_a and clk_b is a clock domain of
    // its own, named by its output: a register on it crosses from either.
    AnalysisRequest req;
    req.input_files = {fx("mux_clock.v")};
    req.top_module = "mux_clock";
    req.input_type = InputType::Netlist;
    req.config_path = std::string(FIXTURES_DIR) + "/config/signoff_policies.yaml";
    Analyzer analyzer;
    auto result = analyzer.run(req);
    ASSERT_TRUE(result.ok) << (result.errors.empty() ? "" : result.errors.front());

    const cdc::Finding* f = find_rule(result.findings, "CDC001");
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(f->source_domain, "clk_a");
    EXPECT_EQ(f->dest_domain, "selected_clk");
}

TEST(NetlistFrontendTest, UnknownCellTreatedAsOpaqueNotSafe) {
    // A cell type absent from the library must be classified Unknown (opaque
    // combinational) and produce a warning — never silently assumed safe.
    AnalysisRequest req;
    req.input_files = {fx("unknown_cell.v")};
    req.top_module = "unknown_cell";
    req.input_type = InputType::Netlist;
    Analyzer analyzer;
    auto result = analyzer.run(req);
    ASSERT_TRUE(result.ok);
    bool warned = false;
    for (const auto& w : result.warnings) {
        if (w.find("unmapped cell") != std::string::npos)
            warned = true;
    }
    EXPECT_TRUE(warned);
}

TEST(NetlistFrontendTest, UnknownSequentialLikeCellForcesIncompleteStatus) {
    // An unmapped cell whose name looks sequential (VENDOR_FF_X1) is very
    // likely a flop; modeling it as opaque combinational drops its crossings.
    // Status must be "incomplete" (and signoff INCOMPLETE) so a reviewer
    // cannot mistake a hole in the graph for a clean run.
    AnalysisRequest req;
    req.input_files = {fx("unknown_sequential_cell.v")};
    req.top_module = "unknown_sequential_cell";
    req.input_type = InputType::Netlist;
    Analyzer analyzer;
    auto result = analyzer.run(req);
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.analysis_status, "incomplete");
    EXPECT_EQ(result.signoff.status, SignoffStatus::Incomplete);
    bool warned = false;
    for (const auto& w : result.warnings) {
        if (w.find("sequential-like") != std::string::npos)
            warned = true;
    }
    EXPECT_TRUE(warned);
}

TEST(NetlistFrontendTest, MappedYosysSyncResetEnableCellsAreNotUnmapped) {
    // $_SDFFE_*/$_SDFFCE_* are built-in; a netlist using them must produce
    // neither the unmapped-cell warning nor an incomplete status.
    AnalysisRequest req;
    req.input_files = {fx("yosys_sdffe.v")};
    req.top_module = "yosys_sdffe";
    req.input_type = InputType::Netlist;
    Analyzer analyzer;
    auto result = analyzer.run(req);
    ASSERT_TRUE(result.ok) << (result.errors.empty() ? "" : result.errors.front());
    EXPECT_EQ(result.analysis_status, "complete");
    for (const auto& w : result.warnings) {
        EXPECT_EQ(w.find("unmapped cell"), std::string::npos) << w;
    }
    // The two flops must exist as registers in the graph (not dropped).
    size_t regs = 0;
    for (const auto& n : result.graph.nodes()) {
        if (n.kind == ir::NodeKind::Register)
            ++regs;
    }
    EXPECT_EQ(regs, 2u);
}

}  // namespace
}  // namespace opencdc::analysis
