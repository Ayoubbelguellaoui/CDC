#ifndef OPENCDC_FRONTEND_SLANG_ADAPTER_H
#define OPENCDC_FRONTEND_SLANG_ADAPTER_H

#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "clock/resolve.h"
#include "ir/graph.h"
#include "slang/text/SourceLocation.h"

namespace slang {
class SourceManager;
}  // namespace slang

namespace slang::ast {
class Compilation;
class InstanceSymbol;
class Scope;
class ProceduralBlockSymbol;
class TimingControl;
}  // namespace slang::ast

namespace opencdc::frontend {

struct ClockResetInfo {
    std::string clock;
    std::string reset;
    ir::ResetPolarity reset_pol = ir::ResetPolarity::None;
    bool is_async_reset = false;
};

struct FrontendResult {
    ir::Graph graph;
    std::vector<std::string> errors;
    bool ok = false;
};

class SlangAdapter {
   public:
    SlangAdapter();
    ~SlangAdapter();

    FrontendResult elaborate(const std::vector<std::string>& files, const std::string& top_module);

   private:
    void walk_instance(const slang::ast::InstanceSymbol& inst, ir::Graph& graph,
                       const std::string& prefix);

    void walk_scope(const slang::ast::Scope& scope, ir::Graph& graph, const std::string& prefix);

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
};

}  // namespace opencdc::frontend

#endif  // OPENCDC_FRONTEND_SLANG_ADAPTER_H
