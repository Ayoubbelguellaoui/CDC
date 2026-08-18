#include "frontend/slang_adapter.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/Symbol.h"
#include "slang/ast/ASTVisitor.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/ast/statements/MiscStatements.h"
#include "slang/ast/statements/ConditionalStatements.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/TimingControl.h"
#include "slang/ast/types/AllTypes.h"
#include "slang/diagnostics/DiagnosticEngine.h"
#include "slang/diagnostics/TextDiagnosticClient.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/text/SourceManager.h"

using namespace slang;
using namespace slang::ast;
using namespace slang::syntax;

namespace opencdc::frontend {

struct AssignInfo {
    std::string lhs;
    std::string rhs;
};

static std::string sv_to_string(std::string_view sv) {
    return std::string(sv);
}

static void collect_assigns(const Statement& stmt,
                            const std::string& prefix,
                            std::vector<AssignInfo>& out) {
    switch (stmt.kind) {
    case StatementKind::Block: {
        auto& blk = static_cast<const BlockStatement&>(stmt);
        collect_assigns(blk.body, prefix, out);
        break;
    }
    case StatementKind::List: {
        auto& list = static_cast<const StatementList&>(stmt);
        for (auto* s : list.list)
            collect_assigns(*s, prefix, out);
        break;
    }
    case StatementKind::Timed: {
        auto& timed = static_cast<const TimedStatement&>(stmt);
        collect_assigns(timed.stmt, prefix, out);
        break;
    }
    case StatementKind::ExpressionStatement: {
        auto& es = static_cast<const ExpressionStatement&>(stmt);
        if (es.expr.kind == ExpressionKind::Assignment) {
            auto& assign = static_cast<const AssignmentExpression&>(es.expr);
            std::string l, r;
            if (assign.left().kind == ExpressionKind::NamedValue)
                l = sv_to_string(static_cast<const NamedValueExpression&>(assign.left()).symbol.name);
            if (assign.right().kind == ExpressionKind::NamedValue)
                r = sv_to_string(static_cast<const NamedValueExpression&>(assign.right()).symbol.name);
            if (!l.empty() && !r.empty())
                out.push_back({prefix + "." + l, prefix + "." + r});
        }
        break;
    }
    case StatementKind::Conditional: {
        auto& ifst = static_cast<const ConditionalStatement&>(stmt);
        collect_assigns(ifst.ifTrue, prefix, out);
        if (ifst.ifFalse)
            collect_assigns(*ifst.ifFalse, prefix, out);
        break;
    }
    default:
        break;
    }
}

ClockResetInfo SlangAdapter::extract_clock_reset(
    const ProceduralBlockSymbol& proc) const {
    ClockResetInfo info;
    info.clock = "unknown";

    const Statement& body = proc.getBody();
    if (body.kind != StatementKind::Timed) return info;

    auto& timed = static_cast<const TimedStatement&>(body);
    auto& timing = timed.timing;

    if (timing.kind == TimingControlKind::SignalEvent) {
        auto& se = static_cast<const SignalEventControl&>(timing);
        if (se.expr.kind == ExpressionKind::NamedValue)
            info.clock = sv_to_string(
                static_cast<const NamedValueExpression&>(se.expr).symbol.name);
    } else if (timing.kind == TimingControlKind::EventList) {
        auto& el = static_cast<const EventListControl&>(timing);
        if (!el.events.empty()) {
            auto& first = *el.events[0];
            if (first.kind == TimingControlKind::SignalEvent) {
                auto& se = static_cast<const SignalEventControl&>(first);
                if (se.expr.kind == ExpressionKind::NamedValue)
                    info.clock = sv_to_string(
                        static_cast<const NamedValueExpression&>(se.expr).symbol.name);
            }
            for (size_t i = 1; i < el.events.size(); ++i) {
                auto& ev = *el.events[i];
                if (ev.kind == TimingControlKind::SignalEvent) {
                    auto& se = static_cast<const SignalEventControl&>(ev);
                    if (se.edge == EdgeKind::NegEdge || se.edge == EdgeKind::PosEdge) {
                        if (se.expr.kind == ExpressionKind::NamedValue) {
                            info.reset = sv_to_string(
                                static_cast<const NamedValueExpression&>(se.expr).symbol.name);
                            info.reset_pol = (se.edge == EdgeKind::NegEdge)
                                ? ir::ResetPolarity::ActiveLow
                                : ir::ResetPolarity::ActiveHigh;
                        }
                    }
                }
            }
        }
    }

    return info;
}

std::optional<ir::SourceLoc> SlangAdapter::to_source_loc(
    const slang::SourceLocation& loc) const {
    if (!loc || !source_manager_) return std::nullopt;
    ir::SourceLoc result;
    result.file = sv_to_string(source_manager_->getFileName(loc));
    result.line = source_manager_->getLineNumber(loc);
    result.col = source_manager_->getColumnNumber(loc);
    return result;
}

SlangAdapter::SlangAdapter()
    : source_manager_(new SourceManager()) {}

SlangAdapter::~SlangAdapter() {
    delete source_manager_;
}

FrontendResult SlangAdapter::elaborate(const std::vector<std::string>& files,
                                       const std::string& top_module) {
    FrontendResult result;
    Compilation compilation;

    for (const auto& file : files) {
        auto treeOrErr = SyntaxTree::fromFile(file, *source_manager_);
        if (!treeOrErr) {
            result.errors.push_back("Failed to read: " + file);
            continue;
        }
        compilation.addSyntaxTree(*treeOrErr);
    }

    const RootSymbol& root = compilation.getRoot();

    Diagnostics diags = compilation.getAllDiagnostics();
    if (!diags.empty()) {
        DiagnosticEngine diagEngine(*source_manager_);
        auto client = std::make_shared<TextDiagnosticClient>();
        diagEngine.addClient(client);
        for (auto& diag : diags) {
            diagEngine.issue(diag);
        }
        std::string diag_text = client->getString();
        if (!diag_text.empty()) {
            result.errors.push_back(diag_text);
        }
        return result;
    }

    const InstanceSymbol* top_inst = nullptr;
    for (auto& member : root.members()) {
        if (member.kind == SymbolKind::Instance) {
            auto& inst = static_cast<const InstanceSymbol&>(member);
            if (inst.name == top_module) {
                top_inst = &inst;
                break;
            }
        }
    }

    if (!top_inst) {
        result.errors.push_back("Top module '" + top_module + "' not found");
        return result;
    }

    walk_instance(*top_inst, result.graph, "");
    resolve_clocks(result.graph);
    result.ok = true;
    return result;
}

void SlangAdapter::walk_instance(const InstanceSymbol& inst,
                                 ir::Graph& graph,
                                 const std::string& prefix) {
    std::string p = prefix.empty() ? sv_to_string(inst.name)
                                   : prefix + "." + sv_to_string(inst.name);

    for (auto& member : inst.body.members()) {
        switch (member.kind) {
        case SymbolKind::Port: {
            auto& port = static_cast<const PortSymbol&>(member);
            std::string port_name = p + "." + sv_to_string(port.name);
            if (!graph.find_node_by_name(port_name)) {
                auto opt_loc = to_source_loc(port.location);
                ir::SourceLoc ir_loc = opt_loc.value_or(ir::SourceLoc{});
                graph.add_port(port_name, 1, ir_loc);
            }
            break;
        }
        case SymbolKind::Instance: {
            auto& child = static_cast<const InstanceSymbol&>(member);
            walk_instance(child, graph, p);
            break;
        }
        case SymbolKind::ProceduralBlock: {
            auto& proc = static_cast<const ProceduralBlockSymbol&>(member);
            if (proc.procedureKind == ProceduralBlockKind::AlwaysFF) {
                ClockResetInfo cr = extract_clock_reset(proc);
                auto opt_loc = to_source_loc(proc.location);
                ir::SourceLoc ir_loc = opt_loc.value_or(ir::SourceLoc{});

                std::vector<AssignInfo> assigns;
                collect_assigns(proc.getBody(), p, assigns);

                for (auto& a : assigns) {
                    const ir::Node* src = graph.find_node_by_name(a.rhs);
                    uint64_t src_id = src ? src->id
                                          : graph.add_port(a.rhs, 1, {});
                    const ir::Node* dst = graph.find_node_by_name(a.lhs);
                    uint64_t dst_id = dst ? dst->id
                                          : graph.add_register(
                                                a.lhs, cr.clock, 1, ir_loc);
                    if (!dst) {
                        auto* mutable_dst = const_cast<ir::Node*>(
                            graph.find_node(dst_id));
                        if (mutable_dst) {
                            mutable_dst->reset_signal = cr.reset;
                            mutable_dst->reset_pol = cr.reset_pol;
                        }
                    }
                    graph.add_edge(src_id, dst_id);
                }
            }
            break;
        }
        default:
            break;
        }
    }
}

void SlangAdapter::resolve_clocks(ir::Graph& graph) {
    clock::ClockResolver resolver;
    auto result = resolver.resolve(graph);

    for (auto& node : graph.nodes_mutable()) {
        if (node.kind != ir::NodeKind::Register) continue;
        if (node.clock_domain.empty() || node.clock_domain == "unknown") continue;

        auto it = result.clock_map.find(node.clock_domain);
        if (it == result.clock_map.end()) continue;

        node.root_clock = it->second.root_clock;
        node.clock_is_gated = it->second.is_gated;
        node.clock_is_muxed = it->second.is_muxed;
    }
}

} // namespace opencdc::frontend
