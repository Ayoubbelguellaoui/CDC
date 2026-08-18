#ifndef OPENCDC_FRONTEND_SLANG_ADAPTER_H
#define OPENCDC_FRONTEND_SLANG_ADAPTER_H

#include "ir/graph.h"
#include "slang/text/SourceLocation.h"
#include "clock/resolve.h"
#include <optional>
#include <string>
#include <vector>

namespace slang {
class SourceManager;
} // namespace slang

namespace slang::ast {
class Compilation;
class InstanceSymbol;
class ProceduralBlockSymbol;
} // namespace slang::ast

namespace opencdc::frontend {

struct ClockResetInfo {
    std::string clock;
    std::string reset;
    ir::ResetPolarity reset_pol = ir::ResetPolarity::None;
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

    FrontendResult elaborate(const std::vector<std::string>& files,
                             const std::string& top_module);

private:
    void walk_instance(const slang::ast::InstanceSymbol& inst,
                       ir::Graph& graph,
                       const std::string& prefix);

    ClockResetInfo extract_clock_reset(
        const slang::ast::ProceduralBlockSymbol& proc) const;

    std::optional<ir::SourceLoc> to_source_loc(
        const slang::SourceLocation& loc) const;

    void resolve_clocks(ir::Graph& graph);

    slang::SourceManager* source_manager_ = nullptr;
};

} // namespace opencdc::frontend

#endif // OPENCDC_FRONTEND_SLANG_ADAPTER_H
