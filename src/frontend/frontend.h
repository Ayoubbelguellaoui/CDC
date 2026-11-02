#ifndef OPENCDC_FRONTEND_FRONTEND_H
#define OPENCDC_FRONTEND_FRONTEND_H

#include <memory>
#include <string>
#include <vector>

#include "ir/graph.h"

namespace opencdc::frontend {

// Language-frontend input selection.
//   Auto    — detect per-file (see detect_input_type); ambiguous → Rtl.
//   Rtl     — behavioral RTL via the slang adapter (historical default).
//   Netlist — flat gate-level Verilog via the netlist adapter.
enum class InputType { Auto, Rtl, Netlist };

// Parse a user-facing input-type string ("auto"/"rtl"/"netlist").
// Returns false + error message on anything else.
bool parse_input_type(const std::string& value, InputType& out, std::string* error = nullptr);

// Human-readable name for reports/verbose output ("rtl"/"netlist").
const char* input_type_name(InputType type);

struct FrontendOptions {
    std::vector<std::string> include_dirs;
    std::vector<std::string> defines;
    bool allow_user_annotation = true;
    // Config::muxed_clock_domain.
    bool muxed_clock_domain = false;
    // YAML cell map path for the netlist frontend (ignored by RTL).
    std::string cell_map_path;
    // Liberty (.lib) file path for the netlist frontend: cell definitions
    // are imported before the YAML cell map, so explicit map entries win.
    std::string liberty_path;
    // RTL->netlist register name map for the netlist frontend ("rtl netlist"
    // per line): netlist registers are renamed to their RTL names so
    // RTL-authored waivers and reports keep matching.
    std::string name_map_path;
    // 0 = built-in defaults (RTL: ir::MAX_GRAPH_*; netlist: kNetlist* below).
    size_t max_nodes = 0;
    size_t max_edges = 0;
};

struct FrontendResult {
    ir::Graph graph;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    bool ok = false;
    // Actual frontend used ("rtl"/"netlist") — surfaced in reports.
    std::string input_type;
    // Netlist frontend: an unmapped cell type whose name looks sequential
    // (DFF/LATCH/FF/...) was treated as opaque combinational. The flop — and
    // every crossing through it — is missing from the graph, so the analysis
    // cannot be "complete"; the analyzer downgrades status to "incomplete"
    // (signoff INCOMPLETE) instead of silently under-reporting.
    bool suspected_sequential_unknown = false;
};

// Common contract for all language frontends (slang RTL, gate-level netlist).
// The analyzer consumes only FrontendResult; downstream stages never see the
// concrete frontend, so rule semantics cannot drift between input types.
class IFrontend {
   public:
    virtual ~IFrontend() = default;

    virtual FrontendResult elaborate(const std::vector<std::string>& files,
                                     const std::string& top_module,
                                     const FrontendOptions& options = {}) = 0;
};

// Conservative per-file heuristic used by InputType::Auto. Reads up to
// kDetectScanBytes of comment-stripped text from each file:
//   - behavioral constructs (always/initial/function/task/...)  → Rtl
//   - only module/wire/port decls, assign, and instantiations    → Netlist
//   - unreadable/ambiguous                                       → Rtl (safe:
//     preserves historical behavior; slang errors surface clearly)
InputType detect_input_type(const std::vector<std::string>& files);

// Factory for the gate-level netlist frontend. Defined in
// frontend/netlist/netlist_adapter.cpp; returns nullptr in builds where the
// netlist frontend is unavailable (the analyzer then reports a clear error).
std::unique_ptr<IFrontend> make_netlist_adapter();

// Netlist-frontend default graph safety limits. Gate-level netlists are 1-2
// orders of magnitude larger than RTL graphs, so the historical 500k/1M caps
// would truncate real designs. Users can still override via --max-nodes/
// --max-edges; exceeding the limit truncates and reports CDC010 as usual.
static constexpr size_t kNetlistMaxNodes = 5000000;
static constexpr size_t kNetlistMaxEdges = 10000000;

}  // namespace opencdc::frontend

#endif  // OPENCDC_FRONTEND_FRONTEND_H
