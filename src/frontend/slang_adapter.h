#ifndef OPENCDC_FRONTEND_SLANG_ADAPTER_H
#define OPENCDC_FRONTEND_SLANG_ADAPTER_H

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "clock/resolve.h"
#include "frontend/frontend.h"
#include "ir/graph.h"
#include "slang/text/SourceLocation.h"

namespace slang {
class SourceManager;
}  // namespace slang

namespace slang::ast {
class Compilation;
class InstanceSymbol;
class Scope;
class Symbol;
class ProceduralBlockSymbol;
class TimingControl;
}  // namespace slang::ast

namespace opencdc::frontend {

struct ClockResetInfo {
    std::string clock;
    // Declaration of the clock when the event is a plain named value, so the
    // clock is named after the scope that declares it (see scope_paths_).
    const slang::ast::Symbol* clock_symbol = nullptr;
    std::string reset;
    ir::ResetPolarity reset_pol = ir::ResetPolarity::None;
    bool is_async_reset = false;
};

// FrontendOptions / FrontendResult / IFrontend / InputType now live in
// frontend/frontend.h (shared contract for all language frontends).

class SlangAdapter : public IFrontend {
   public:
    SlangAdapter();
    ~SlangAdapter() override;

    FrontendResult elaborate(const std::vector<std::string>& files, const std::string& top_module,
                             const FrontendOptions& options = {}) override;

   private:
    void walk_instance(const slang::ast::InstanceSymbol& inst, ir::Graph& graph,
                       const std::string& prefix);

    void walk_scope(const slang::ast::Scope& scope, ir::Graph& graph, const std::string& prefix,
                    const std::string& module_type);

    ClockResetInfo extract_clock_reset(const slang::ast::ProceduralBlockSymbol& proc) const;

    // Best-effort signal name for a signal-event expression (symbol name for
    // named values, source text otherwise).
    std::string event_signal_name(const slang::ast::TimingControl& event) const;

    std::optional<ir::SourceLoc> to_source_loc(const slang::SourceLocation& loc) const;

    void resolve_clocks(ir::Graph& graph);

    static bool is_top_level_input(const std::string& hier_name, const ir::Graph& graph,
                                   const std::unordered_set<std::string>& top_level_ports);

    static std::string trace_to_top_level_source(const std::string& clock_name,
                                                 const ir::Graph& graph);

    std::unique_ptr<slang::SourceManager> source_manager_;
    bool allow_user_annotation_ = true;
    // Non-fatal elaboration notes (e.g. skipped unknown-clock blocks).
    std::vector<std::string> frontend_warnings_;
    // Recursion depth guard for instance/generate nesting (walk_* recursion).
    size_t scope_depth_ = 0;
    static constexpr size_t kMaxScopeDepth = 64;
    // Hierarchical path of every scope walk_scope entered (instance bodies,
    // generate blocks). A signal is named after the scope that declares it,
    // not the one that reads it: a generate block reads its module's signals.
    std::unordered_map<const slang::ast::Scope*, std::string> scope_paths_;
};

}  // namespace opencdc::frontend

#endif  // OPENCDC_FRONTEND_SLANG_ADAPTER_H
