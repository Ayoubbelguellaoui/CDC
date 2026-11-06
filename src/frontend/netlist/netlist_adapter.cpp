#include "frontend/netlist/netlist_adapter.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "cell/liberty.h"
#include "clock/resolve.h"
#include "frontend/netlist/flatten.h"
#include "frontend/netlist/name_map.h"
#include "frontend/netlist/netlist_parser.h"
#include "frontend/netlist/regroup.h"
#include "ir/compact.h"

namespace opencdc::frontend::netlist {

namespace {

using ir::EdgeRole;
using ir::LogicType;
using ir::NodeKind;
using netlist::Conn;
using netlist::Instance;
using netlist::NetlistModule;
using netlist::NetRef;
using netlist::PortDir;

bool is_gate_primitive(const std::string& cell_type) {
    static const std::unordered_set<std::string> kGates = {
        "and", "or",     "not",    "nand",   "nor",    "xor",    "xnor",
        "buf", "bufif0", "bufif1", "notif0", "notif1", "pullup", "pulldown"};
    return kGates.count(cell_type) > 0;
}

// Map a combinational gate/cell name to a LogicType (approximate for
// inverting gates — netlist connectivity, not exact function, drives CDC).
LogicType logic_for_gate(const std::string& g) {
    if (g == "and" || g == "nand")
        return LogicType::And;
    if (g == "or" || g == "nor")
        return LogicType::Or;
    if (g == "xor" || g == "xnor")
        return LogicType::Xor;
    if (g == "not" || g == "notif0" || g == "notif1")
        return LogicType::Not;
    if (g == "buf" || g == "bufif0" || g == "bufif1")
        return LogicType::Buf;
    if (g == "mux")
        return LogicType::Mux;
    return LogicType::Unknown;
}

// Heuristic: does an unmapped cell type name look like a sequential element?
// Used to force analysis_status=incomplete for signoff rather than silently
// modeling the cell as opaque combinational (which would hide crossings).
// Matches the substrings every major Yosys/vendor flop/latch family carries:
// "FF" covers DFF/SDFFE/SDFFCE/ALDFF/DFFSR/RSDFF/TFF/JKFF/..., "LATCH"/"LAT"
// covers level-sensitive cells, "REG" covers *REG_* library flops. Purely
// combinational names (AND/OR/XOR/MUX/AOI/OAI/BUF/INV/CLKGATE/ISO) never
// match, so the heuristic cannot fire on known comb cells.
bool looks_sequential_name(const std::string& cell_type) {
    static const char* kNeedles[] = {"FF", "LATCH", "LAT", "REG"};
    for (const char* n : kNeedles) {
        if (cell_type.find(n) != std::string::npos)
            return true;
    }
    return false;
}

// Pin-role candidate names, in preference order. Used when the cell library
// entry doesn't specify explicit pin names.
const std::vector<std::string>& clock_pins() {
    static const std::vector<std::string> p = {"CK", "CLK", "C", "GCLK", "CP", "clock"};
    return p;
}
const std::vector<std::string>& data_pins() {
    static const std::vector<std::string> p = {"D", "DI", "DATA", "D1"};
    return p;
}
const std::vector<std::string>& output_pins() {
    static const std::vector<std::string> p = {"Q", "QN", "Z", "Y", "CO", "OUT"};
    return p;
}
const std::vector<std::string>& enable_pins() {
    static const std::vector<std::string> p = {"E", "EN", "CE", "enable", "TE"};
    return p;
}
const std::vector<std::string>& reset_pins() {
    static const std::vector<std::string> p = {"R",   "RN",  "RST",     "RESET", "CDN",
                                               "CLR", "SRN", "RESET_B", "RSTN"};
    return p;
}
const std::vector<std::string>& set_pins() {
    static const std::vector<std::string> p = {"S", "SN", "SET", "SDN", "SP", "PB"};
    return p;
}
// ICG-specific.
const std::vector<std::string>& icg_out_pins() {
    static const std::vector<std::string> p = {"GCK", "CLKOUT", "Q", "Y", "GCLK"};
    return p;
}

// Find a connection whose pin matches any candidate (exact, then case-insensitive).
const Conn* find_pin(const std::vector<Conn>& conns, const std::vector<std::string>& cands) {
    for (const auto& c : conns) {
        for (const auto& cand : cands) {
            if (c.pin == cand)
                return &c;
        }
    }
    for (const auto& c : conns) {
        for (const auto& cand : cands) {
            if (c.pin.size() == cand.size()) {
                bool ci = true;
                for (size_t i = 0; i < c.pin.size(); ++i) {
                    if (std::tolower(static_cast<unsigned char>(c.pin[i])) !=
                        std::tolower(static_cast<unsigned char>(cand[i]))) {
                        ci = false;
                        break;
                    }
                }
                if (ci)
                    return &c;
            }
        }
    }
    return nullptr;
}

// Prepend an explicit pin (from the cell library) as the highest-priority
// candidate so user-specified names win over the generic heuristics.
std::vector<std::string> with_explicit(const std::string& explicit_pin,
                                       const std::vector<std::string>& generic) {
    std::vector<std::string> v;
    if (!explicit_pin.empty())
        v.push_back(explicit_pin);
    v.insert(v.end(), generic.begin(), generic.end());
    return v;
}

// Strip a trailing `[bit]` (or `[msb:lsb]`) select to get the base net name.
std::string strip_bit(const std::string& name) {
    size_t i = name.find('[');
    return i == std::string::npos ? name : name.substr(0, i);
}

// The node identity for a net reference. Bit-selects of internal nets are
// distinct nodes (`bus[0]` vs `bus[1]`) so cross-bit connectivity is not
// fabricated; whole-net references map to the base name. Ports are resolved to
// their single port node regardless of bit (see GraphBuilder::get_net_node).
std::string signal_of(const NetRef& ref) {
    if (ref.kind != NetRef::Kind::Net)
        return "";
    if (ref.has_bit)
        return ref.net + "[" + std::to_string(ref.bit) + "]";
    return ref.net;
}

class GraphBuilder {
   public:
    GraphBuilder(ir::Graph& graph, const NetlistModule& mod, const cell::CellLibrary& lib,
                 std::string top, std::string filename)
        : graph_(graph),
          mod_(mod),
          lib_(lib),
          top_(std::move(top)),
          filename_(std::move(filename)) {}

    void build(std::vector<std::string>& warnings);

    // True when an unmapped cell with a sequential-like name was dropped as
    // opaque combinational during build (surfaced on FrontendResult so the
    // analyzer can force status=incomplete for signoff).
    bool suspected_sequential_unknown() const {
        return suspected_sequential_unknown_;
    }

   private:
    // Union-find over net names for `assign a = b` pass-throughs (ports pinned).
    std::string find(const std::string& x) {
        auto it = parent_.find(x);
        if (it == parent_.end() || it->second == x)
            return x;
        std::string root = find(it->second);
        it->second = root;
        return root;
    }
    void alias(const std::string& driven, const std::string& driver) {
        if (driven.empty() || driver.empty() || driven == driver)
            return;
        if (ports_.count(strip_bit(driven))) {
            // A top-level port keeps its own node; record the feed for later.
            port_feed_[strip_bit(driven)] = driver;
            return;
        }
        std::string rd = find(driven);
        std::string rv = find(driver);
        if (rd != rv)
            parent_[rd] = rv;
    }

    ir::SourceLoc loc(size_t line) const {
        return ir::SourceLoc{filename_, static_cast<uint32_t>(line), 0};
    }
    uint32_t net_width(const std::string& name) const {
        if (name.find('[') != std::string::npos)
            return 1;  // per-bit node
        auto it = net_width_.find(name);
        return it != net_width_.end() ? it->second : 1;
    }

    uint64_t port_node(const std::string& name);
    uint64_t net_node(const std::string& name);
    // Node id for a net reference, resolving aliases, bits, and ports.
    uint64_t get_node(const NetRef& ref);
    // Node id for a signal string (`base` or `base[bit]`), resolving aliases
    // and collapsing any bit of a top-level port to the single port node.
    uint64_t get_net_node(const std::string& signal);

    void build_assign_aliases();
    void process_instance(const Instance& inst, std::vector<std::string>& warnings);
    void process_sequential(const Instance& inst, const cell::CellDef* def, cell::CellKind kind,
                            std::vector<std::string>& warnings);
    void process_icg(const Instance& inst, const cell::CellDef* def,
                     std::vector<std::string>& warnings);
    void process_comb(const Instance& inst, const cell::CellDef* def, cell::CellKind kind,
                      std::vector<std::string>& warnings);
    void process_gate_primitive(const Instance& inst, std::vector<std::string>& warnings);

    ir::Graph& graph_;
    const NetlistModule& mod_;
    const cell::CellLibrary& lib_;
    std::string top_;
    std::string filename_;

    std::unordered_set<std::string> ports_;
    std::unordered_map<std::string, std::string> parent_;     // union-find
    std::unordered_map<std::string, std::string> port_feed_;  // port -> driver net
    std::unordered_map<std::string, uint32_t> net_width_;
    std::unordered_map<std::string, uint64_t> port_nodes_;
    std::unordered_map<std::string, uint64_t> net_nodes_;
    std::unordered_set<std::string> unknown_cells_;
    // An unmapped cell whose name looks sequential (DFF/LATCH/FF/...) was
    // treated as opaque combinational — the flop is missing from the graph.
    bool suspected_sequential_unknown_ = false;
};

uint64_t GraphBuilder::port_node(const std::string& name) {
    auto it = port_nodes_.find(name);
    if (it != port_nodes_.end())
        return it->second;
    uint64_t id = graph_.add_port(name, net_width(name), ir::SourceLoc{filename_, 0, 0});
    port_nodes_[name] = id;
    return id;
}

uint64_t GraphBuilder::net_node(const std::string& name) {
    auto it = net_nodes_.find(name);
    if (it != net_nodes_.end())
        return it->second;
    uint64_t id = graph_.add_net(name, net_width(name), ir::SourceLoc{filename_, 0, 0});
    net_nodes_[name] = id;
    return id;
}

uint64_t GraphBuilder::get_net_node(const std::string& signal) {
    std::string r = find(signal);
    if (r.empty())
        return 0;
    if (ports_.count(strip_bit(r)))
        return port_node(strip_bit(r));
    return net_node(r);
}

uint64_t GraphBuilder::get_node(const NetRef& ref) {
    if (ref.kind == NetRef::Kind::Const)
        return 0;  // constant ties have no driver node
    if (ref.kind == NetRef::Kind::Concat) {
        // MVP: concat has no single node; callers should not use get_node on
        // concat refs for edges. Return 0.
        return 0;
    }
    return get_net_node(signal_of(ref));
}

void GraphBuilder::build_assign_aliases() {
    for (const auto& a : mod_.assigns) {
        // Simple `x = y` (both plain net refs) alias by signal identity so
        // bit-selects resolve to the correct per-bit node. Concat/const RHS are
        // handled with explicit edges in build() pass 2.
        if (a.lhs.kind == NetRef::Kind::Net && a.rhs.kind == NetRef::Kind::Net) {
            alias(signal_of(a.lhs), signal_of(a.rhs));
        }
    }
}

void GraphBuilder::build(std::vector<std::string>& warnings) {
    // Record widths and the port set.
    for (const auto& p : mod_.ports) {
        ports_.insert(p.name);
        net_width_[p.name] = p.width();
    }
    for (const auto& n : mod_.nets) {
        net_width_[n.name] = n.width();
        parent_[n.name] = n.name;
    }

    build_assign_aliases();

    // Create all top-level port nodes up front.
    for (const auto& p : mod_.ports) {
        port_node(p.name);
    }

    // Pass 1: functional instances (registers, comb, ICG, gate primitives).
    for (const auto& inst : mod_.instances) {
        process_instance(inst, warnings);
        if (graph_.truncated())
            return;
    }

    // Pass 2: simple assigns that were NOT aliased (bit/concat) get explicit
    // net nodes + edges.
    for (const auto& a : mod_.assigns) {
        if (a.lhs.kind == NetRef::Kind::Net && a.rhs.kind == NetRef::Kind::Net)
            continue;  // already aliased
        uint64_t lhs_node = get_node(a.lhs);
        if (a.rhs.kind == NetRef::Kind::Const) {
            // Constant tie: no driver edge (leave the net undriven).
            if (lhs_node == 0)
                net_node(a.lhs.net);
            continue;
        }
        if (a.rhs.kind == NetRef::Kind::Concat) {
            for (const auto& m : a.rhs.concat) {
                uint64_t mn = get_net_node(m);
                if (mn && lhs_node)
                    graph_.add_edge(mn, lhs_node, EdgeRole::Data);
            }
            continue;
        }
        // Simple bit-select / net on the RHS.
        uint64_t rhs_node = get_node(a.rhs);
        if (rhs_node && lhs_node)
            graph_.add_edge(rhs_node, lhs_node, EdgeRole::Data);
    }

    // Pass 3: connect port feeds (output ports driven by aliased buffers/assigns).
    for (const auto& [port, driver] : port_feed_) {
        uint64_t pn = port_node(port);
        uint64_t dn = get_net_node(driver);
        if (pn && dn)
            graph_.add_edge(dn, pn, EdgeRole::Data);
    }

    // Aggregate unknown-cell warnings.
    if (!unknown_cells_.empty()) {
        std::ostringstream oss;
        bool first = true;
        for (const auto& c : unknown_cells_) {
            if (!first)
                oss << ", ";
            oss << c;
            first = false;
        }
        warnings.push_back(
            "netlist: unmapped cell type(s) treated as opaque combinational (add entries to the "
            "cell map to classify them): " +
            oss.str());
        if (suspected_sequential_unknown_) {
            std::ostringstream oss_seq;
            bool first_seq = true;
            for (const auto& c : unknown_cells_) {
                if (!looks_sequential_name(c))
                    continue;
                if (!first_seq)
                    oss_seq << ", ";
                oss_seq << c;
                first_seq = false;
            }
            if (!oss_seq.str().empty()) {
                warnings.push_back(
                    "netlist: unknown cell type(s) with sequential-like names treated as opaque "
                    "combinational; crossings through these cells are missing from the analysis "
                    "(add entries to the cell map): " +
                    oss_seq.str());
            }
        }
    }
}

void GraphBuilder::process_instance(const Instance& inst, std::vector<std::string>& warnings) {
    if (is_gate_primitive(inst.cell_type)) {
        process_gate_primitive(inst, warnings);
        return;
    }
    if (inst.positional()) {
        warnings.push_back("netlist: positional cell instance '" + inst.inst_name + "' (" +
                           inst.cell_type + ") skipped; named pin connections are required");
        return;
    }
    const cell::CellDef* def = lib_.lookup(inst.cell_type);
    cell::CellKind kind = def ? def->kind : cell::CellKind::Unknown;
    switch (kind) {
        case cell::CellKind::Dff:
        case cell::CellKind::DffReset:
        case cell::CellKind::DffEnable:
        case cell::CellKind::DffSetReset:
        case cell::CellKind::Latch:
        case cell::CellKind::Retention:  // save/restore flop: sequential like Dff
            process_sequential(inst, def, kind, warnings);
            return;
        case cell::CellKind::Icg:
            process_icg(inst, def, warnings);
            return;
        case cell::CellKind::Isolation:  // transparent clamp: combinational like Buffer
        default:
            process_comb(inst, def, kind, warnings);
            return;
    }
}

void GraphBuilder::process_sequential(const Instance& inst, const cell::CellDef* def,
                                      cell::CellKind kind, std::vector<std::string>& warnings) {
    cell::CellKind k = kind;
    std::string clk_pin = def ? def->clock_pin : "";
    std::string d_pin = def ? def->data_pin : "";
    std::string q_pin = def ? def->output_pin : "";
    std::string rst_pin = def ? def->reset_pin : "";
    std::string set_pin = def ? def->set_pin : "";
    std::string en_pin = def ? def->enable_pin : "";

    const Conn* clk = find_pin(inst.conns, with_explicit(clk_pin, clock_pins()));
    const Conn* d = find_pin(inst.conns, with_explicit(d_pin, data_pins()));
    const Conn* q = find_pin(inst.conns, with_explicit(q_pin, output_pins()));
    const Conn* rst = find_pin(inst.conns, with_explicit(rst_pin, reset_pins()));
    const Conn* setp = find_pin(inst.conns, with_explicit(set_pin, set_pins()));
    const Conn* en = find_pin(inst.conns, with_explicit(en_pin, enable_pins()));

    if (!clk || clk->net.kind != NetRef::Kind::Net) {
        warnings.push_back("netlist: sequential cell '" + inst.inst_name + "' (" + inst.cell_type +
                           ") has no identifiable clock pin; treated as opaque");
        unknown_cells_.insert(inst.cell_type);
        // A classified-sequential cell that failed to bind its clock is still
        // sequential — its crossings are missing, so signoff must be incomplete.
        suspected_sequential_unknown_ = true;
        return;
    }

    // Latches use the gate/enable net as their "clock"; flops use the clock.
    std::string clk_net = find(signal_of(clk->net));
    if (k == cell::CellKind::Latch && en && en->net.kind == NetRef::Kind::Net)
        clk_net = find(signal_of(en->net));

    uint64_t reg = graph_.add_register(inst.inst_name, clk_net, /*width=*/1, loc(inst.line),
                                       /*module_path=*/"", inst.cell_type);
    if (reg == 0)
        return;
    if (k == cell::CellKind::Latch) {
        ir::Node* n = graph_.find_node_mutable(reg);
        if (n)
            n->is_latch = true;
    }

    // Reset properties from the cell definition.
    if (rst && rst->net.kind == NetRef::Kind::Net) {
        ir::Node* n = graph_.find_node_mutable(reg);
        if (n) {
            n->reset_signal = find(signal_of(rst->net));
            bool active_low = def && def->reset_active_low;
            n->reset_pol =
                active_low ? ir::ResetPolarity::ActiveLow : ir::ResetPolarity::ActiveHigh;
            n->is_async_reset = def && def->reset_async;
        }
    }
    (void)setp;  // set pin present but not modelled (rare; name is recorded via cell_type)

    // Data edge: D net -> register.
    if (d && d->net.kind != NetRef::Kind::Const) {
        uint64_t dn = get_node(d->net);
        if (dn)
            graph_.add_edge(dn, reg, EdgeRole::Data);
    }
    // Output edge: register -> Q net.
    if (q && q->net.kind == NetRef::Kind::Net) {
        uint64_t qn = get_node(q->net);
        if (qn)
            graph_.add_edge(reg, qn, EdgeRole::Data);
    }
}

void GraphBuilder::process_icg(const Instance& inst, const cell::CellDef* def,
                               std::vector<std::string>& warnings) {
    std::string clk_pin = def ? def->clock_pin : "";
    std::string en_pin = def ? def->enable_pin : "";
    std::string out_pin = def ? def->output_pin : "";

    const Conn* clk = find_pin(inst.conns, with_explicit(clk_pin, clock_pins()));
    const Conn* en = find_pin(inst.conns, with_explicit(en_pin, enable_pins()));
    const Conn* out = find_pin(inst.conns, with_explicit(out_pin, icg_out_pins()));

    if (!clk || !out || clk->net.kind != NetRef::Kind::Net || out->net.kind != NetRef::Kind::Net) {
        warnings.push_back("netlist: clock-gate cell '" + inst.inst_name + "' (" + inst.cell_type +
                           ") missing clock or output pin; treated as opaque");
        unknown_cells_.insert(inst.cell_type);
        return;
    }

    uint64_t clk_n = get_node(clk->net);
    uint64_t out_n = get_node(out->net);
    std::vector<uint64_t> inputs;
    if (clk_n)
        inputs.push_back(clk_n);
    if (en && en->net.kind == NetRef::Kind::Net) {
        uint64_t en_n = get_node(en->net);
        if (en_n)
            inputs.push_back(en_n);
    }
    // Model the gate as AND(clock, enable) so ClockResolver flags the gated net.
    uint64_t and_n = graph_.add_combinational(inst.inst_name + "/gate", LogicType::And, inputs,
                                              /*width=*/1, loc(inst.line), "", "icg");
    if (and_n && out_n)
        graph_.add_edge(and_n, out_n, EdgeRole::Data);
}

void GraphBuilder::process_comb(const Instance& inst, const cell::CellDef* def, cell::CellKind kind,
                                std::vector<std::string>& warnings) {
    if (kind == cell::CellKind::Unknown) {
        unknown_cells_.insert(inst.cell_type);
        // An unmapped cell whose name looks sequential (DFF/LATCH/FF/...) is
        // very likely a flop that will be modeled as opaque combinational —
        // every crossing through it disappears. Flag it so signoff reports
        // INCOMPLETE instead of a false "complete".
        if (looks_sequential_name(inst.cell_type))
            suspected_sequential_unknown_ = true;
    }

    // Determine output pin (first matching output candidate) and inputs (rest).
    std::string out_pin = def ? def->output_pin : "";
    const Conn* out = nullptr;
    if (!out_pin.empty())
        out = find_pin(inst.conns, {out_pin});
    if (!out)
        out = find_pin(inst.conns, output_pins());

    LogicType lt = LogicType::Unknown;
    if (kind == cell::CellKind::Buffer)
        lt = LogicType::Buf;
    else if (kind == cell::CellKind::Inverter)
        lt = LogicType::Not;
    else if (def && def->logic_type_hint >= 0)
        lt = static_cast<LogicType>(def->logic_type_hint);  // Liberty function
    else if (def && !def->pattern.empty()) {
        // Best-effort logic type from the pattern name for Comb cells.
        // Order matters: XOR/XNOR must be tested before AND/NOR/OR because
        // "XOR" and "XNOR" contain them as substrings, and NAND before OR.
        const std::string& p = def->pattern;
        if (p.find("XOR") != std::string::npos || p.find("XNOR") != std::string::npos)
            lt = LogicType::Xor;
        else if (p.find("NAND") != std::string::npos || p.find("AND") != std::string::npos)
            lt = LogicType::And;
        else if (p.find("NOR") != std::string::npos || p.find("OR") != std::string::npos)
            lt = LogicType::Or;
        else if (p.find("MUX") != std::string::npos)
            lt = LogicType::Mux;
    }

    std::vector<uint64_t> inputs;
    // Named inputs (pin -> node) kept for mux role normalization below.
    std::vector<std::pair<std::string, uint64_t>> named_inputs;
    for (const auto& c : inst.conns) {
        if (out && &c == out)
            continue;  // skip output pin
        if (c.net.kind == NetRef::Kind::Net) {
            uint64_t n = get_node(c.net);
            if (n) {
                inputs.push_back(n);
                named_inputs.emplace_back(c.pin, n);
            }
        }
    }

    if (lt == LogicType::Mux) {
        // Normalize mux inputs to the shared IR operand convention
        // [select, true, false]: the RTL frontend stores a ternary
        // `cond ? t : f` in exactly that order, and ClockResolver treats the
        // LAST input of a mux as its representative (default-path, select=0)
        // root clock. Standard-cell pin roles (yosys `$_MUX_`: Y = S ? B : A;
        // library muxes: D1/I1 = select-1 input, D0/I0 = select-0 input) are
        // mapped into that order so RTL and netlist resolve muxed clocks to
        // the same root. Unrecognized pin names keep connection order.
        size_t sel = named_inputs.size(), t = named_inputs.size(), f = named_inputs.size();
        for (size_t i = 0; i < named_inputs.size(); ++i) {
            const std::string& pin = named_inputs[i].first;
            if (sel == named_inputs.size() && (pin == "S" || pin == "SEL" || pin == "SELECT")) {
                sel = i;
            } else if (f == named_inputs.size() && (pin == "A" || pin == "D0" || pin == "I0")) {
                f = i;  // select-0 input
            } else if (t == named_inputs.size() && (pin == "B" || pin == "D1" || pin == "I1")) {
                t = i;  // select-1 input
            }
        }
        if (sel < named_inputs.size() && t < named_inputs.size() && f < named_inputs.size()) {
            inputs = {named_inputs[sel].second, named_inputs[t].second, named_inputs[f].second};
        }
    }

    uint64_t comb = graph_.add_combinational(inst.inst_name, lt, inputs, /*width=*/1,
                                             loc(inst.line), "", inst.cell_type);
    if (comb && out && out->net.kind == NetRef::Kind::Net) {
        uint64_t on = get_node(out->net);
        if (on)
            graph_.add_edge(comb, on, EdgeRole::Data);
    }
    if (comb && kind == cell::CellKind::Isolation) {
        if (ir::Node* n = graph_.find_node_mutable(comb))
            n->is_isolation_cell = true;
    }
}

void GraphBuilder::process_gate_primitive(const Instance& inst,
                                          std::vector<std::string>& warnings) {
    const std::string& g = inst.cell_type;
    if (g == "bufif0" || g == "bufif1" || g == "notif0" || g == "notif1" || g == "pullup" ||
        g == "pulldown") {
        warnings.push_back("netlist: tristate/pull gate '" + inst.inst_name + "' (" + g +
                           ") skipped");
        return;
    }
    if (inst.conns.size() < 2) {
        warnings.push_back("netlist: gate primitive '" + inst.inst_name + "' has too few ports");
        return;
    }
    // Positional gate primitives: first port is the output, the rest are inputs.
    const NetRef& out = inst.conns[0].net;
    LogicType lt = logic_for_gate(g);
    std::vector<uint64_t> inputs;
    for (size_t i = 1; i < inst.conns.size(); ++i) {
        if (inst.conns[i].net.kind == NetRef::Kind::Net) {
            uint64_t n = get_node(inst.conns[i].net);
            if (n)
                inputs.push_back(n);
        }
    }
    uint64_t node =
        graph_.add_combinational(inst.inst_name, lt, inputs, /*width=*/1, loc(inst.line), "", g);
    if (node && out.kind == NetRef::Kind::Net) {
        uint64_t on = get_node(out);
        if (on)
            graph_.add_edge(node, on, EdgeRole::Data);
    }
}

}  // namespace

FrontendResult NetlistAdapter::elaborate(const std::vector<std::string>& files,
                                         const std::string& top_module,
                                         const FrontendOptions& options) {
    FrontendResult result;
    result.input_type = "netlist";

    // Read input files.
    std::vector<std::string> sources;
    std::vector<std::string> filenames;
    for (const auto& path : files) {
        std::ifstream in(path, std::ios::binary);
        if (!in.is_open()) {
            result.errors.push_back("Failed to read: " + path);
            return result;
        }
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        sources.push_back(std::move(content));
        filenames.push_back(path);
    }
    if (sources.empty()) {
        result.errors.push_back("netlist: no input files");
        return result;
    }

    // Parse.
    ParseResult parsed = parse_netlist(sources, filenames, top_module);
    result.errors = parsed.errors;
    result.warnings = parsed.warnings;
    if (!parsed.ok)
        return result;

    // Hierarchical elaboration: when any instance of the top module refers to
    // another module defined in the design, flatten the hierarchy into one
    // module (child contents get `inst.`-prefixed names; child ports bind
    // through the parent-side pin connections) before building the graph.
    // Flat netlists — the common synthesized case — skip the pass entirely.
    const NetlistModule* top = &parsed.module;
    NetlistModule flat;
    if (references_submodules(parsed.module, parsed.modules)) {
        std::vector<std::string> hier_errors;
        if (!flatten_hierarchy(parsed.module, parsed.modules, flat, hier_errors, result.warnings)) {
            for (auto& e : hier_errors)
                result.errors.push_back(e);
            return result;
        }
        top = &flat;
    }

    // Cell library: defaults + optional Liberty import + optional YAML map.
    // Precedence (lowest → highest): heuristic defaults, Liberty cell
    // definitions (exact names, so they beat the short default globs), and
    // the YAML cell map last (user's explicit overrides win ties).
    library_ = cell::CellLibrary::with_defaults();
    if (!options.liberty_path.empty()) {
        cell::CellLibrary liberty_defs;
        std::string lib_err;
        if (!cell::load_liberty_file(options.liberty_path, liberty_defs, &lib_err)) {
            result.errors.push_back(lib_err);
            return result;
        }
        for (const auto& d : liberty_defs.defs())
            library_.add(d);
    }
    if (!options.cell_map_path.empty()) {
        cell::CellLibrary overlay;
        std::string cell_err;
        if (!cell::CellLibrary::load_from_yaml_file(options.cell_map_path, overlay, &cell_err)) {
            result.errors.push_back(cell_err);
            return result;
        }
        for (const auto& d : overlay.defs())
            library_.add(d);
    }

    // Graph safety limits (netlist defaults are larger than RTL).
    size_t max_nodes = options.max_nodes ? options.max_nodes : kNetlistMaxNodes;
    size_t max_edges = options.max_edges ? options.max_edges : kNetlistMaxEdges;
    result.graph.set_limits(max_nodes, max_edges);

    // Build the IR graph.
    GraphBuilder builder(result.graph, *top, library_, top_module, filenames.front());
    std::vector<std::string> build_warnings;
    builder.build(build_warnings);
    result.warnings.insert(result.warnings.end(), build_warnings.begin(), build_warnings.end());
    result.suspected_sequential_unknown = builder.suspected_sequential_unknown();

    // RTL-to-netlist register name mapping, applied right after build and
    // BEFORE gray recovery/regrouping so bit groups merge under their RTL
    // names and RTL-authored waivers match netlist findings.
    if (!options.name_map_path.empty()) {
        std::vector<std::pair<std::string, std::string>> nm_entries;
        std::string nm_err;
        if (!netlist::load_name_map(options.name_map_path, nm_entries, &nm_err)) {
            result.errors.push_back(nm_err);
            return result;
        }
        apply_name_map(result.graph, nm_entries, &result.warnings);
    }

    // Netlist post-passes on the built graph, in order:
    //  1. Structural gray-code recovery — re-recognizes the `x ^ (x>>1)`
    //     encoder shape synthesis left behind and re-annotates `is_gray_coded`
    //     so CDC002 reaches the same verdict as the RTL analysis. Must run
    //     before regrouping (it annotates the bit flops that get folded).
    //  2. Multi-bit bus regrouping — re-merge `<base>[i>` bit flops into one
    //     wide register so the analyzer sees RTL register granularity
    //     (per-pair counts, CDC002 bus width). Must run before clock
    //     resolution so merged registers are annotated exactly like any other
    //     register; compaction below then collapses the now pass-through
    //     per-bit nets.
    annotate_gray_registers(result.graph);
    regroup_buses(result.graph, result.warnings);

    // Resolve clocks: propagate root_clock / gated / muxed onto register nodes
    // so CDC004 (gated-clock crossing) and clock-domain grouping work exactly as
    // they do for RTL. Same pass the slang adapter runs.
    clock::ClockResolver clock_resolver;
    clock_resolver.set_muxed_clock_domain(options.muxed_clock_domain);
    auto clock_result = clock_resolver.resolve(result.graph);
    for (auto& node : result.graph.nodes_mutable()) {
        if (node.kind != ir::NodeKind::Register)
            continue;
        if (node.clock_domain.empty() || node.clock_domain == "unknown")
            continue;
        auto it = clock_result.clock_map.find(node.clock_domain);
        if (it != clock_result.clock_map.end()) {
            node.root_clock = it->second.root_clock;
            node.clock_is_gated = it->second.is_gated;
            if (!node.clock_is_muxed)
                node.clock_is_muxed = it->second.is_muxed;
        }
    }
    for (auto& w : clock_result.warnings)
        result.warnings.push_back(std::move(w));

    if (result.graph.truncated()) {
        result.errors.push_back(
            "netlist: graph exceeded safety limit during build; analysis incomplete");
        return result;
    }

    // Collapse identity buffer cells and pass-through data nets so large
    // synthesized graphs stay cheap to traverse. Clock / reset structure is
    // preserved (see ir/compact.h); registers, ports, and reachability are
    // untouched, so findings are identical to the uncompacted graph.
    ir::compact_graph(result.graph);

    auto validation = result.graph.validate();
    if (!validation.ok()) {
        result.errors = validation.errors;
        return result;
    }

    result.ok = true;
    return result;
}

}  // namespace opencdc::frontend::netlist

namespace opencdc::frontend {

// Factory referenced by the analyzer. Declared in frontend/frontend.h (the
// frontend namespace) so the analyzer depends only on the shared contract.
std::unique_ptr<IFrontend> make_netlist_adapter() {
    return std::make_unique<netlist::NetlistAdapter>();
}

}  // namespace opencdc::frontend
