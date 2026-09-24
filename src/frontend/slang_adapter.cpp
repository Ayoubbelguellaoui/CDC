#include "frontend/slang_adapter.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/Symbol.h"
#include "slang/ast/TimingControl.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/CallExpression.h"
#include "slang/ast/expressions/ConversionExpression.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/expressions/SelectExpressions.h"
#include "slang/ast/statements/ConditionalStatements.h"
#include "slang/ast/statements/LoopStatements.h"
#include "slang/ast/statements/MiscStatements.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/SubroutineSymbols.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/ast/types/AllTypes.h"
#include "slang/diagnostics/DiagnosticEngine.h"
#include "slang/diagnostics/TextDiagnosticClient.h"
#include "slang/parsing/Preprocessor.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/text/SourceManager.h"
#include "slang/util/Bag.h"

using namespace slang;
using namespace slang::ast;
using namespace slang::syntax;

namespace opencdc::frontend {

struct AssignInfo {
    std::string lhs;
    std::string rhs;
    uint32_t lhs_width = 1;
    uint32_t rhs_width = 1;
    bool is_gray_transform = false;
    ir::LogicType logic_type = ir::LogicType::Unknown;
    bool is_multi_operand = false;
};

struct MultiAssignInfo {
    std::string lhs;
    uint32_t lhs_width = 1;
    std::vector<std::pair<std::string, uint32_t>> rhs_operands;
};

static std::string sv_to_string(std::string_view sv) {
    return std::string(sv);
}

// A gray transform is `x ^ (x >> 1)` or `x ^ (x << 1)` (either operand
// order): one operand is the signal itself, the other the same signal
// shifted. Requiring the shared signal keeps arbitrary XOR expressions
// like `a ^ (b + c)` from being flagged as gray-coded.
static bool is_gray_xor_structure(const Expression& expr) {
    if (expr.kind != ExpressionKind::BinaryOp)
        return false;
    auto& binary = static_cast<const BinaryExpression&>(expr);
    if (binary.op != BinaryOperator::BinaryXor)
        return false;

    auto is_shift_of = [](const Expression& shifted, const Expression& base) -> bool {
        if (shifted.kind != ExpressionKind::BinaryOp)
            return false;
        auto& sh = static_cast<const BinaryExpression&>(shifted);
        if (sh.op != BinaryOperator::LogicalShiftRight &&
            sh.op != BinaryOperator::LogicalShiftLeft &&
            sh.op != BinaryOperator::ArithmeticShiftRight &&
            sh.op != BinaryOperator::ArithmeticShiftLeft) {
            return false;
        }
        return sh.left().kind == ExpressionKind::NamedValue &&
               base.kind == ExpressionKind::NamedValue &&
               &static_cast<const NamedValueExpression&>(sh.left()).symbol ==
                   &static_cast<const NamedValueExpression&>(base).symbol;
    };

    return (binary.left().kind == ExpressionKind::NamedValue &&
            is_shift_of(binary.right(), binary.left())) ||
           (binary.right().kind == ExpressionKind::NamedValue &&
            is_shift_of(binary.left(), binary.right()));
}

// Naming heuristics for CDC-safe patterns the parser cannot otherwise see.
// Only used as a seed: detection still requires cross-domain pairing before
// any crossing is treated as verified/safe, so a coincidental name match on a
// single signal cannot suppress findings on its own.
static void apply_pattern_name_tags(ir::Node& node) {
    size_t last_dot = node.hier_name.rfind('.');
    std::string base =
        (last_dot == std::string::npos) ? node.hier_name : node.hier_name.substr(last_dot + 1);
    std::string l = base;
    std::transform(l.begin(), l.end(), l.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    bool valid_named = l.rfind("valid", 0) == 0 || l.find("_valid") != std::string::npos;
    bool ready_named = l.rfind("ready", 0) == 0 || l.find("_ready") != std::string::npos;
    if (valid_named || ready_named) {
        node.is_handshake_signal = true;
    }

    bool fifo_ptr_named =
        l.find("wr_ptr") != std::string::npos || l.find("write_ptr") != std::string::npos ||
        l.find("wptr") != std::string::npos || l.find("rd_ptr") != std::string::npos ||
        l.find("read_ptr") != std::string::npos || l.find("rptr") != std::string::npos;
    if (fifo_ptr_named) {
        node.is_async_fifo_ptr = true;
    }
}

static std::optional<std::pair<std::string, uint32_t>> extract_lvalue(const Expression& expr,
                                                                      const std::string& prefix) {
    if (expr.kind == ExpressionKind::NamedValue) {
        auto& nv = static_cast<const NamedValueExpression&>(expr);
        return std::make_pair(prefix + "." + sv_to_string(nv.symbol.name),
                              static_cast<uint32_t>(expr.type->getBitWidth()));
    }
    if (expr.kind == ExpressionKind::MemberAccess) {
        auto& member = static_cast<const MemberAccessExpression&>(expr);
        auto base = extract_lvalue(member.value(), prefix);
        if (!base)
            return std::nullopt;
        base->first += "." + sv_to_string(member.member.name);
        base->second = static_cast<uint32_t>(expr.type->getBitWidth());
        return base;
    }
    if (expr.kind == ExpressionKind::ElementSelect) {
        auto& select = static_cast<const ElementSelectExpression&>(expr);
        auto base = extract_lvalue(select.value(), prefix);
        if (!base)
            return std::nullopt;
        base->second = static_cast<uint32_t>(expr.type->getBitWidth());
        return base;
    }
    if (expr.kind == ExpressionKind::RangeSelect) {
        auto& select = static_cast<const RangeSelectExpression&>(expr);
        auto base = extract_lvalue(select.value(), prefix);
        if (!base)
            return std::nullopt;
        base->second = static_cast<uint32_t>(expr.type->getBitWidth());
        return base;
    }
    return std::nullopt;
}

using FormalMap = std::unordered_map<std::string, std::pair<std::string, uint32_t>>;

static void collect_named_values(const Expression& expr, const std::string& prefix,
                                 std::vector<std::pair<std::string, uint32_t>>& out,
                                 const FormalMap& formal_map = {});

// Map a call-site actual to signal names. Output arguments arrive wrapped
// as Assignment expressions (slang represents `task(a, out_sig)` outputs
// that way); only the driven lvalue names a signal. Scoped to call-actual
// mapping so general operand collection (ports, continuous assigns) keeps
// its long-standing behavior.
static void collect_actual_values(const Expression& actual, const std::string& prefix,
                                  std::vector<std::pair<std::string, uint32_t>>& vals,
                                  const FormalMap& subst = {}) {
    if (actual.kind == ExpressionKind::Assignment) {
        // Output actual: the driven lvalue is the signal. (Caller-side
        // signals need no formal substitution.)
        auto& asgn = static_cast<const AssignmentExpression&>(actual);
        auto lhs = extract_lvalue(asgn.left(), prefix);
        if (lhs)
            vals.push_back(*lhs);
        return;
    }
    collect_named_values(actual, prefix, vals, subst);
}

static void collect_named_values(const Expression& expr, const std::string& prefix,
                                 std::vector<std::pair<std::string, uint32_t>>& out,
                                 const FormalMap& formal_map) {
    if (expr.kind == ExpressionKind::NamedValue) {
        auto& nv = static_cast<const NamedValueExpression&>(expr);
        std::string raw_name = sv_to_string(nv.symbol.name);
        auto fm_it = formal_map.find(raw_name);
        std::string name;
        uint32_t width = 1;
        if (fm_it != formal_map.end()) {
            name = fm_it->second.first;
            width = fm_it->second.second;
        } else {
            name = prefix + "." + raw_name;
            if (nv.symbol.kind == SymbolKind::Variable) {
                auto& var = static_cast<const VariableSymbol&>(nv.symbol);
                width = var.getType().getBitWidth();
            }
        }
        out.push_back({name, width});
    } else if (expr.kind == ExpressionKind::BinaryOp) {
        auto& binary = static_cast<const BinaryExpression&>(expr);
        collect_named_values(binary.left(), prefix, out, formal_map);
        collect_named_values(binary.right(), prefix, out, formal_map);
    } else if (expr.kind == ExpressionKind::ConditionalOp) {
        auto& cond = static_cast<const ConditionalExpression&>(expr);
        for (auto& c : cond.conditions)
            collect_named_values(*c.expr, prefix, out, formal_map);
        collect_named_values(cond.left(), prefix, out, formal_map);
        collect_named_values(cond.right(), prefix, out, formal_map);
    } else if (expr.kind == ExpressionKind::UnaryOp) {
        auto& unary = static_cast<const UnaryExpression&>(expr);
        collect_named_values(unary.operand(), prefix, out, formal_map);
    } else if (expr.kind == ExpressionKind::MemberAccess) {
        auto& ma = static_cast<const MemberAccessExpression&>(expr);
        collect_named_values(ma.value(), prefix, out, formal_map);
    } else if (expr.kind == ExpressionKind::ElementSelect) {
        auto& es = static_cast<const ElementSelectExpression&>(expr);
        collect_named_values(es.value(), prefix, out, formal_map);
        collect_named_values(es.selector(), prefix, out, formal_map);
    } else if (expr.kind == ExpressionKind::RangeSelect) {
        auto& rs = static_cast<const RangeSelectExpression&>(expr);
        collect_named_values(rs.value(), prefix, out, formal_map);
    } else if (expr.kind == ExpressionKind::Concatenation) {
        auto& cat = static_cast<const ConcatenationExpression&>(expr);
        for (auto& op : cat.operands())
            collect_named_values(*op, prefix, out, formal_map);
    } else if (expr.kind == ExpressionKind::Replication) {
        auto& rep = static_cast<const ReplicationExpression&>(expr);
        collect_named_values(rep.concat(), prefix, out, formal_map);
    } else if (expr.kind == ExpressionKind::Call) {
        auto& call = static_cast<const CallExpression&>(expr);
        if (!call.isSystemCall()) {
            for (auto* arg : call.arguments()) {
                if (arg)
                    collect_named_values(*arg, prefix, out, formal_map);
            }
        }
    } else if (expr.kind == ExpressionKind::Conversion) {
        auto& conv = static_cast<const ConversionExpression&>(expr);
        collect_named_values(conv.operand(), prefix, out, formal_map);
    }
}

// Shared NamedValue symbol of a gray-xor `x ^ (x >> 1)`; nullptr otherwise.
// Same predicate as is_gray_xor_structure, but also reports *which* signal
// so function-boundary analysis can match it against formal arguments.
static const Symbol* gray_xor_shared_symbol(const Expression& expr) {
    if (!is_gray_xor_structure(expr))
        return nullptr;
    auto& binary = static_cast<const BinaryExpression&>(expr);
    if (binary.left().kind == ExpressionKind::NamedValue)
        return &static_cast<const NamedValueExpression&>(binary.left()).symbol;
    return &static_cast<const NamedValueExpression&>(binary.right()).symbol;
}

// Does user function `call` compute `x ^ (x >> 1)` over one of its formals,
// where that formal is (transitively) fed by a named signal? Covers
// `gray <= bin2gray(bin)` without inlining the whole dataflow, including
// nested calls (`outer(x)` calling `inner(x)`), up to a small depth.
// `subst` maps enclosing-scope formal names to concrete signal names so
// inner actuals that reference outer formals resolve transitively.
static bool call_computes_gray_of_named(const CallExpression& call, const std::string& prefix,
                                        const FormalMap& subst, size_t depth) {
    if (call.isSystemCall() || depth > 4)
        return false;
    auto* sub_ptr = std::get_if<const SubroutineSymbol*>(&call.subroutine);
    if (!sub_ptr || !*sub_ptr)
        return false;
    const SubroutineSymbol* sub = *sub_ptr;
    auto formals = sub->getArguments();
    auto actuals = call.arguments();
    if (formals.empty() || actuals.empty())
        return false;

    // Map formal name -> concrete signal fed at this call site. Only the
    // first named operand is tracked (documented multi-operand limitation).
    FormalMap local;
    for (size_t i = 0; i < formals.size() && i < actuals.size(); ++i) {
        std::vector<std::pair<std::string, uint32_t>> vals;
        collect_actual_values(*actuals[i], prefix, vals, subst);
        if (!vals.empty())
            local[sv_to_string(formals[i]->name)] = vals[0];
    }

    // Candidate result expressions: `return <expr>;` and assignments to the
    // function return variable. Bounded walk (no loops/recursion).
    std::vector<const Expression*> candidates;
    std::vector<std::pair<const Statement*, size_t>> work = {{&sub->getBody(), 0}};
    std::string return_name = sv_to_string(sub->name);
    while (!work.empty() && candidates.size() < 32) {
        auto [stmt, stmt_depth] = work.back();
        work.pop_back();
        if (!stmt || stmt_depth > 8)
            continue;
        switch (stmt->kind) {
            case StatementKind::Block: {
                auto& blk = static_cast<const BlockStatement&>(*stmt);
                work.emplace_back(&blk.body, stmt_depth + 1);
                break;
            }
            case StatementKind::List: {
                auto& list = static_cast<const StatementList&>(*stmt);
                for (auto* st : list.list)
                    work.emplace_back(st, stmt_depth + 1);
                break;
            }
            case StatementKind::Return: {
                auto& ret = static_cast<const ReturnStatement&>(*stmt);
                if (ret.expr)
                    candidates.push_back(ret.expr);
                break;
            }
            case StatementKind::ExpressionStatement: {
                auto& es = static_cast<const ExpressionStatement&>(*stmt);
                if (es.expr.kind == ExpressionKind::Assignment) {
                    auto& asgn = static_cast<const AssignmentExpression&>(es.expr);
                    if (asgn.left().kind == ExpressionKind::NamedValue) {
                        auto& lhs = static_cast<const NamedValueExpression&>(asgn.left()).symbol;
                        if (sv_to_string(lhs.name) == return_name || &lhs == sub->returnValVar)
                            candidates.push_back(&asgn.right());
                    }
                } else if (es.expr.kind == ExpressionKind::Call) {
                    // Nested call as a bare statement: its output formals may
                    // feed this function's result indirectly; approximate by
                    // treating a nested gray-computing call as a candidate.
                    // (Deeper dataflow through task outputs is out of scope.)
                    candidates.push_back(&es.expr);
                }
                break;
            }
            default:
                break;
        }
    }

    auto is_formal = [&](const Symbol* sym) {
        for (auto* f : formals) {
            if (sym == f)
                return true;
        }
        return false;
    };
    for (const Expression* cand : candidates) {
        if (const Symbol* shared = gray_xor_shared_symbol(*cand)) {
            if (is_formal(shared) && local.find(sv_to_string(shared->name)) != local.end())
                return true;
        } else if (cand->kind == ExpressionKind::Call) {
            auto& inner = static_cast<const CallExpression&>(*cand);
            if (call_computes_gray_of_named(inner, prefix, local, depth + 1))
                return true;
        }
    }
    return false;
}

// Does user function `call` compute `x ^ (x >> 1)` over one of its formals,
// where that formal is fed by a named signal at the call site? Covers
// `gray <= bin2gray(bin)` without inlining the whole dataflow: the outer
// assignment is tagged as a gray transform of the actuals.
static bool function_computes_gray(const CallExpression& call, const std::string& prefix) {
    return call_computes_gray_of_named(call, prefix, {}, 0);
}

static void collect_assigns(const Statement& stmt, const std::string& prefix,
                            std::vector<AssignInfo>& out, const FormalMap& formal_map = {},
                            size_t depth = 0) {
    switch (stmt.kind) {
        case StatementKind::Block: {
            auto& blk = static_cast<const BlockStatement&>(stmt);
            collect_assigns(blk.body, prefix, out, formal_map, depth);
            break;
        }
        case StatementKind::List: {
            auto& list = static_cast<const StatementList&>(stmt);
            for (auto* s : list.list)
                collect_assigns(*s, prefix, out, formal_map, depth);
            break;
        }
        case StatementKind::Timed: {
            auto& timed = static_cast<const TimedStatement&>(stmt);
            collect_assigns(timed.stmt, prefix, out, formal_map, depth);
            break;
        }
        case StatementKind::ExpressionStatement: {
            auto& es = static_cast<const ExpressionStatement&>(stmt);
            if (es.expr.kind == ExpressionKind::Assignment) {
                auto& assign = static_cast<const AssignmentExpression&>(es.expr);
                auto lhs = extract_lvalue(assign.left(), prefix);
                if (lhs) {
                    std::string l = lhs->first.substr(prefix.size() + 1);
                    uint32_t l_width = lhs->second;
                    auto fm_it = formal_map.find(l);
                    std::string resolved_lhs;
                    if (fm_it != formal_map.end()) {
                        resolved_lhs = fm_it->second.first;
                        l_width = fm_it->second.second;
                    } else {
                        resolved_lhs = prefix + "." + l;
                    }
                    std::vector<std::pair<std::string, uint32_t>> rhs_ops;
                    collect_named_values(assign.right(), prefix, rhs_ops, formal_map);
                    bool is_gray_transform = false;
                    ir::LogicType logic_type = ir::LogicType::Unknown;
                    bool is_multi_operand = false;
                    if (assign.right().kind == ExpressionKind::BinaryOp) {
                        auto& binary = static_cast<const BinaryExpression&>(assign.right());
                        if (is_gray_xor_structure(assign.right())) {
                            is_gray_transform = true;
                        }
                        if (binary.op == BinaryOperator::BinaryAnd)
                            logic_type = ir::LogicType::And;
                        else if (binary.op == BinaryOperator::BinaryOr)
                            logic_type = ir::LogicType::Or;
                        else if (binary.op == BinaryOperator::BinaryXor)
                            logic_type = ir::LogicType::Xor;
                        is_multi_operand = true;
                    } else if (assign.right().kind == ExpressionKind::ConditionalOp) {
                        logic_type = ir::LogicType::Mux;
                        is_multi_operand = true;
                    } else if (assign.right().kind == ExpressionKind::UnaryOp) {
                        auto& unary = static_cast<const UnaryExpression&>(assign.right());
                        if (unary.op == UnaryOperator::BitwiseNot ||
                            unary.op == UnaryOperator::LogicalNot) {
                            logic_type = ir::LogicType::Not;
                        } else if (unary.op == UnaryOperator::BitwiseAnd) {
                            logic_type = ir::LogicType::And;
                        } else if (unary.op == UnaryOperator::BitwiseOr) {
                            logic_type = ir::LogicType::Or;
                        } else if (unary.op == UnaryOperator::BitwiseXor ||
                                   unary.op == UnaryOperator::BitwiseXnor) {
                            logic_type = ir::LogicType::Xor;
                        }
                        is_multi_operand = true;
                    } else if (assign.right().kind == ExpressionKind::Call) {
                        // User function on the RHS (e.g. gray <= bin2gray(bin)):
                        // attribute a gray transform computed inside the function
                        // body to this assignment so the encoder node is built.
                        auto& call = static_cast<const CallExpression&>(assign.right());
                        if (function_computes_gray(call, prefix)) {
                            is_gray_transform = true;
                            logic_type = ir::LogicType::Xor;
                            is_multi_operand = true;
                        }
                    }

                    if (rhs_ops.empty()) {
                        // Literal-only or non-NamedValue RHS (e.g. q <= 1'b0).
                        // Push an AssignInfo with empty RHS so the register node
                        // is created in the main processing loop.
                        out.push_back({resolved_lhs, std::string(), l_width, 0, is_gray_transform,
                                       logic_type, false});
                    } else if (rhs_ops.size() == 1 && !is_multi_operand) {
                        out.push_back({resolved_lhs, rhs_ops[0].first, l_width, rhs_ops[0].second,
                                       is_gray_transform, logic_type, false});
                    } else if (rhs_ops.size() >= 1) {
                        for (auto& [r, r_width] : rhs_ops) {
                            out.push_back({resolved_lhs, r, l_width, r_width, is_gray_transform,
                                           logic_type, true});
                        }
                    }
                }
            } else if (es.expr.kind == ExpressionKind::Call) {
                auto& call = static_cast<const CallExpression&>(es.expr);
                if (!call.isSystemCall()) {
                    auto* sub_ptr = std::get_if<const SubroutineSymbol*>(&call.subroutine);
                    if (sub_ptr && *sub_ptr) {
                        const auto* sub = *sub_ptr;
                        auto formals = sub->getArguments();
                        auto actuals = call.arguments();

                        FormalMap fmap;
                        for (size_t i = 0; i < formals.size() && i < actuals.size(); ++i) {
                            std::string formal_name = sv_to_string(formals[i]->name);
                            std::vector<std::pair<std::string, uint32_t>> vals;
                            collect_actual_values(*actuals[i], prefix, vals);
                            if (!vals.empty()) {
                                fmap[formal_name] = vals[0];
                            }
                        }

                        std::vector<AssignInfo> body_assigns;
                        if (depth < 16) {
                            collect_assigns(sub->getBody(), prefix, body_assigns, fmap, depth + 1);
                        }
                        for (auto& a : body_assigns) {
                            out.push_back(std::move(a));
                        }
                    }
                }
            }
            break;
        }
        case StatementKind::Conditional: {
            auto& ifst = static_cast<const ConditionalStatement&>(stmt);
            collect_assigns(ifst.ifTrue, prefix, out, formal_map, depth);
            if (ifst.ifFalse)
                collect_assigns(*ifst.ifFalse, prefix, out, formal_map, depth);
            break;
        }
        case StatementKind::ForLoop: {
            auto& fl = static_cast<const ForLoopStatement&>(stmt);
            collect_assigns(fl.body, prefix, out, formal_map, depth);
            break;
        }
        case StatementKind::WhileLoop: {
            auto& wl = static_cast<const WhileLoopStatement&>(stmt);
            collect_assigns(wl.body, prefix, out, formal_map, depth);
            break;
        }
        case StatementKind::DoWhileLoop: {
            auto& dwl = static_cast<const DoWhileLoopStatement&>(stmt);
            collect_assigns(dwl.body, prefix, out, formal_map, depth);
            break;
        }
        case StatementKind::RepeatLoop: {
            auto& rl = static_cast<const RepeatLoopStatement&>(stmt);
            collect_assigns(rl.body, prefix, out, formal_map, depth);
            break;
        }
        case StatementKind::ForeachLoop: {
            auto& fl = static_cast<const ForeachLoopStatement&>(stmt);
            collect_assigns(fl.body, prefix, out, formal_map, depth);
            break;
        }
        case StatementKind::ForeverLoop: {
            auto& fl = static_cast<const ForeverLoopStatement&>(stmt);
            collect_assigns(fl.body, prefix, out, formal_map, depth);
            break;
        }
        case StatementKind::Case: {
            auto& cs = static_cast<const CaseStatement&>(stmt);
            for (auto& item : cs.items)
                collect_assigns(*item.stmt, prefix, out, formal_map, depth);
            if (cs.defaultCase)
                collect_assigns(*cs.defaultCase, prefix, out, formal_map, depth);
            break;
        }
        case StatementKind::PatternCase: {
            auto& pcs = static_cast<const PatternCaseStatement&>(stmt);
            for (auto& item : pcs.items)
                collect_assigns(*item.stmt, prefix, out, formal_map, depth);
            if (pcs.defaultCase)
                collect_assigns(*pcs.defaultCase, prefix, out, formal_map, depth);
            break;
        }
        default:
            break;
    }
}

// Conservative reset-name heuristic: only signals whose (lowercased) name
// clearly denotes a reset are treated as reset events. Everything else in an
// event list is a clock candidate. This prevents the common async-reset form
// `@(posedge rst_n or posedge clk)` from swapping clock and reset roles
// regardless of event order.
static bool is_reset_name(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    // Strip bus suffixes like [0] for robustness.
    size_t bracket = lower.find('[');
    if (bracket != std::string::npos)
        lower = lower.substr(0, bracket);
    // Token-boundary match: split on non-alnum and match whole tokens.
    // Avoids "rst" matching "first"/"burst"/"worst" and "clr" matching "clearance".
    auto is_reset_token = [](const std::string& t) {
        if (t == "rst" || t == "rstn" || t == "reset" || t == "resetn" || t == "arst" ||
            t == "arstn" || t == "clr" || t == "clear" || t == "preset" || t == "presetn")
            return true;
        // rstN / resetN / rst0 forms without underscore (e.g. "rstn", "reset2").
        if ((t.rfind("rst", 0) == 0 || t.rfind("reset", 0) == 0) && t.size() <= 7) {
            bool rest_ok = true;
            size_t prefix = (t.rfind("reset", 0) == 0) ? 5 : 3;
            for (size_t i = prefix; i < t.size(); ++i) {
                char c = t[i];
                if (c != 'n' && c != '_' && !std::isdigit(static_cast<unsigned char>(c)))
                    rest_ok = false;
            }
            if (rest_ok)
                return true;
        }
        return false;
    };
    std::string cur;
    for (size_t i = 0; i <= lower.size(); ++i) {
        char c = (i < lower.size()) ? lower[i] : '_';
        if (std::isalnum(static_cast<unsigned char>(c))) {
            cur += c;
        } else {
            if (!cur.empty() && is_reset_token(cur))
                return true;
            cur.clear();
        }
    }
    return false;
    // NOTE: "set" prefix heuristic removed — it is too broad and falsely matches
    // set_valid, set_data, set_enable etc. "preset" is already caught above.
}

// Extract a readable signal name from an arbitrary event expression. Named
// values use the symbol name; anything else (hierarchical refs, selects) is
// recovered from source text so the always block is not silently dropped.
std::string SlangAdapter::event_signal_name(const TimingControl& event) const {
    if (event.kind != TimingControlKind::SignalEvent)
        return "";
    auto& se = static_cast<const SignalEventControl&>(event);
    if (se.expr.kind == ExpressionKind::NamedValue) {
        return sv_to_string(static_cast<const NamedValueExpression&>(se.expr).symbol.name);
    }
    if (!source_manager_)
        return "";
    SourceRange range = se.expr.sourceRange;
    if (!range.start().buffer())
        return "";
    std::string_view buf = source_manager_->getSourceText(range.start().buffer());
    if (range.end().offset() <= range.start().offset() || range.end().offset() > buf.size()) {
        return "";
    }
    std::string text(
        buf.substr(range.start().offset(), range.end().offset() - range.start().offset()));
    // Trim leading/trailing whitespace, drop inner whitespace (source slices
    // like "clk " vs "clk" must not become distinct domains), and strip
    // a trailing [digits] bit-select ("clk[0]" vs "clk"), mirroring
    // is_reset_name. Interior brackets belong to generate-block indices
    // ("gen[0].clk" stays distinct from "gen[1].clk").
    std::string compact;
    for (char c : text) {
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
            compact += c;
    }
    if (compact.empty())
        return "";
    size_t lb = compact.rfind('[');
    if (lb != std::string::npos && compact.back() == ']') {
        bool digits = lb + 1 < compact.size() - 1;
        for (size_t i = lb + 1; digits && i + 1 < compact.size(); ++i)
            digits = std::isdigit(static_cast<unsigned char>(compact[i])) != 0;
        if (digits)
            compact = compact.substr(0, lb);
    }
    return compact;
}

ClockResetInfo SlangAdapter::extract_clock_reset(const ProceduralBlockSymbol& proc) const {
    ClockResetInfo info;
    info.clock = "unknown";

    const Statement& body = proc.getBody();
    if (body.kind != StatementKind::Timed)
        return info;

    auto& timed = static_cast<const TimedStatement&>(body);
    auto& timing = timed.timing;

    if (timing.kind == TimingControlKind::SignalEvent) {
        std::string name = event_signal_name(timing);
        if (!name.empty())
            info.clock = name;
    } else if (timing.kind == TimingControlKind::EventList) {
        auto& el = static_cast<const EventListControl&>(timing);
        // Classify each event by role (reset vs clock) instead of position.
        // The clock is the last non-reset edge event, matching the Verilog
        // convention `@(posedge reset or posedge clk)`.
        std::string clock_candidate;
        for (auto& ev : el.events) {
            if (!ev || ev->kind != TimingControlKind::SignalEvent)
                continue;
            auto& se = static_cast<const SignalEventControl&>(*ev);
            if (se.edge != EdgeKind::NegEdge && se.edge != EdgeKind::PosEdge)
                continue;

            std::string name = event_signal_name(*ev);
            if (name.empty())
                continue;

            if (is_reset_name(name)) {
                info.reset = name;
                info.reset_pol = (se.edge == EdgeKind::NegEdge) ? ir::ResetPolarity::ActiveLow
                                                                : ir::ResetPolarity::ActiveHigh;
                info.is_async_reset = true;
            } else {
                clock_candidate = name;
            }
        }
        if (!clock_candidate.empty()) {
            info.clock = clock_candidate;
        } else if (!el.events.empty()) {
            // All events matched the reset heuristic — fall back to the first
            // edge event as clock so the block is not dropped entirely.
            for (auto& ev : el.events) {
                if (!ev || ev->kind != TimingControlKind::SignalEvent)
                    continue;
                auto& se = static_cast<const SignalEventControl&>(*ev);
                if (se.edge != EdgeKind::NegEdge && se.edge != EdgeKind::PosEdge)
                    continue;
                std::string name = event_signal_name(*ev);
                if (!name.empty()) {
                    info.clock = name;
                    break;
                }
            }
        }
    }

    return info;
}

std::optional<ir::SourceLoc> SlangAdapter::to_source_loc(const slang::SourceLocation& loc) const {
    if (!loc || !source_manager_)
        return std::nullopt;
    ir::SourceLoc result;
    std::string file = sv_to_string(source_manager_->getFileName(loc));
    // Prefer cwd-relative paths in reports (less absolute-path leakage into
    // JSON/SARIF/HTML) when the file is under the working directory.
    // Cached once: cwd never changes during elaboration.
    static const std::string cwd_prefix = [] {
        std::error_code ec;
        std::string cwd = std::filesystem::current_path(ec).string();
        if (!ec && !cwd.empty() && cwd.back() != '/')
            cwd += '/';
        return ec ? std::string() : cwd;
    }();
    if (!cwd_prefix.empty() && file.compare(0, cwd_prefix.size(), cwd_prefix) == 0)
        file = file.substr(cwd_prefix.size());
    result.file = std::move(file);
    result.line = source_manager_->getLineNumber(loc);
    result.col = source_manager_->getColumnNumber(loc);
    return result;
}

SlangAdapter::SlangAdapter() : source_manager_(std::make_unique<SourceManager>()) {}

SlangAdapter::~SlangAdapter() = default;

FrontendResult SlangAdapter::elaborate(const std::vector<std::string>& files,
                                       const std::string& top_module,
                                       const FrontendOptions& options) {
    FrontendResult result;
    Compilation compilation;
    allow_user_annotation_ = options.allow_user_annotation;
    frontend_warnings_.clear();
    scope_depth_ = 0;

    // Fail fast on bad frontend options with actionable errors instead of
    // cryptic slang diagnostics downstream.
    for (const auto& dir : options.include_dirs) {
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec) || ec) {
            result.errors.push_back("Invalid include directory (not a directory): " + dir);
            result.ok = false;
            return result;
        }
    }
    for (const auto& def : options.defines) {
        std::string name = def.substr(0, def.find('='));
        bool valid =
            !name.empty() && (std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_');
        for (size_t i = 1; valid && i < name.size(); ++i) {
            char c = name[i];
            valid = std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        }
        if (!valid) {
            result.errors.push_back("Invalid macro definition (expected NAME or NAME=value): " +
                                    def);
            result.ok = false;
            return result;
        }
    }

    slang::parsing::PreprocessorOptions pp;
    for (const auto& dir : options.include_dirs) {
        auto ec = source_manager_->addUserDirectories(dir);
        if (ec) {
            result.errors.push_back("Invalid include directory: " + dir);
            result.ok = false;
            return result;
        }
        pp.additionalIncludePaths.emplace_back(dir);
    }
    for (const auto& def : options.defines) {
        if (def.find('=') != std::string::npos)
            pp.predefines.push_back(def);
        else
            pp.predefines.push_back(def + "=1");
    }
    slang::Bag bag;
    bag.set(pp);

    for (const auto& file : files) {
        auto treeOrErr = SyntaxTree::fromFile(file, *source_manager_, bag);
        if (!treeOrErr) {
            result.errors.push_back("Failed to read: " + file);
            result.ok = false;
            return result;
        }
        compilation.addSyntaxTree(*treeOrErr);
    }

    const RootSymbol& root = compilation.getRoot();

    Diagnostics diags = compilation.getAllDiagnostics();
    bool has_errors = false;
    for (auto& diag : diags) {
        if (diag.isError())
            has_errors = true;
    }
    if (has_errors) {
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
        std::string hint = "Top module '" + top_module + "' not found";
        // List available top-level candidates to help with typos.
        std::vector<std::string> candidates;
        for (auto& member : root.members()) {
            if (member.kind == SymbolKind::Instance) {
                auto& inst = static_cast<const InstanceSymbol&>(member);
                candidates.push_back(std::string(inst.name));
                if (candidates.size() >= 5)
                    break;
            }
        }
        if (!candidates.empty()) {
            hint += ". Available modules:";
            for (auto& c : candidates)
                hint += " " + c;
        }
        result.errors.push_back(std::move(hint));
        return result;
    }

    walk_instance(*top_inst, result.graph, "");
    if (result.graph.truncated()) {
        result.errors.push_back("IR graph exceeded node or edge safety limit; analysis incomplete");
        return result;
    }
    resolve_clocks(result.graph);
    auto validation = result.graph.validate();
    if (!validation.ok()) {
        result.errors = validation.errors;
        return result;
    }
    result.warnings = std::move(frontend_warnings_);
    result.ok = true;
    return result;
}

void SlangAdapter::walk_instance(const InstanceSymbol& inst, ir::Graph& graph,
                                 const std::string& prefix) {
    std::string p =
        prefix.empty() ? sv_to_string(inst.name) : prefix + "." + sv_to_string(inst.name);
    std::string type = sv_to_string(inst.getDefinition().name);
    walk_scope(inst.body, graph, p, type);
}

void SlangAdapter::walk_scope(const slang::ast::Scope& scope, ir::Graph& graph,
                              const std::string& p, const std::string& module_type) {
    if (scope_depth_ >= kMaxScopeDepth) {
        frontend_warnings_.push_back("Scope nesting exceeds " + std::to_string(kMaxScopeDepth) +
                                     " levels at '" + p + "'; deeper hierarchy skipped");
        return;
    }
    ++scope_depth_;
    for (auto& member : scope.members()) {
        switch (member.kind) {
            case SymbolKind::Port: {
                auto& port = static_cast<const PortSymbol&>(member);
                std::string port_name = p + "." + sv_to_string(port.name);
                if (!graph.find_node_by_name(port_name)) {
                    auto opt_loc = to_source_loc(port.location);
                    ir::SourceLoc ir_loc = opt_loc.value_or(ir::SourceLoc{});
                    uint32_t port_width = 1;
                    const auto& port_type = port.getType();
                    if (port_type.isIntegral()) {
                        port_width = static_cast<uint32_t>(port_type.getBitWidth());
                        if (port_width == 0)
                            port_width = 1;
                    }
                    graph.add_port(port_name, port_width, ir_loc, p, module_type);
                }
                break;
            }
            case SymbolKind::Instance: {
                auto& child = static_cast<const InstanceSymbol&>(member);
                std::string child_type = sv_to_string(child.getDefinition().name);
                walk_scope(child.body, graph, p + "." + sv_to_string(child.name), child_type);

                const std::string child_prefix = p + "." + sv_to_string(child.name);
                for (const auto* connection : child.getPortConnections()) {
                    if (!connection || connection->port.kind != SymbolKind::Port)
                        continue;

                    const auto& child_port = static_cast<const PortSymbol&>(connection->port);
                    const Expression* expression = connection->getExpression();
                    if (!expression)
                        continue;

                    std::vector<std::pair<std::string, uint32_t>> parent_values;
                    collect_named_values(*expression, p, parent_values);
                    if (parent_values.empty())
                        continue;

                    const std::string child_port_name =
                        child_prefix + "." + sv_to_string(child_port.name);
                    const ir::Node* child_node = graph.find_node_by_name(child_port_name);
                    if (!child_node)
                        continue;

                    for (const auto& [parent_name, parent_width] : parent_values) {
                        const ir::Node* parent_node = graph.find_node_by_name(parent_name);
                        uint64_t parent_id = parent_node ? parent_node->id
                                                         : graph.add_net(parent_name, parent_width,
                                                                         {}, p, module_type);

                        if (child_port.direction == ArgumentDirection::Out) {
                            graph.add_edge(child_node->id, parent_id);
                        } else if (child_port.direction == ArgumentDirection::In) {
                            graph.add_edge(parent_id, child_node->id);
                        } else {
                            graph.add_edge(parent_id, child_node->id);
                            graph.add_edge(child_node->id, parent_id);
                        }
                    }
                }
                break;
            }
            case SymbolKind::GenerateBlock: {
                auto& block = static_cast<const GenerateBlockSymbol&>(member);
                if (!block.isUninstantiated) {
                    walk_scope(block, graph, p + "." + sv_to_string(block.name), module_type);
                }
                break;
            }
            case SymbolKind::GenerateBlockArray: {
                auto& array = static_cast<const GenerateBlockArraySymbol&>(member);
                std::string array_prefix = p + "." + sv_to_string(array.name);
                for (const auto* entry : array.entries) {
                    if (entry && !entry->isUninstantiated) {
                        walk_scope(*entry, graph, array_prefix + "." + sv_to_string(entry->name),
                                   module_type);
                    }
                }
                break;
            }
            case SymbolKind::ProceduralBlock: {
                auto& proc = static_cast<const ProceduralBlockSymbol&>(member);
                if (proc.procedureKind == ProceduralBlockKind::AlwaysFF ||
                    proc.procedureKind == ProceduralBlockKind::Always ||
                    proc.procedureKind == ProceduralBlockKind::AlwaysLatch ||
                    proc.procedureKind == ProceduralBlockKind::AlwaysComb) {
                    ClockResetInfo cr;
                    if (proc.procedureKind == ProceduralBlockKind::AlwaysFF ||
                        proc.procedureKind == ProceduralBlockKind::Always ||
                        proc.procedureKind == ProceduralBlockKind::AlwaysLatch) {
                        cr = extract_clock_reset(proc);
                        // Skip blocks with no recoverable clock so "unknown" never
                        // becomes a real domain name — applies to all clocked block types
                        // including AlwaysLatch. Surfaced as a warning (not silent)
                        // so missed crossings can be investigated.
                        if (cr.clock == "unknown") {
                            auto opt_wloc = to_source_loc(proc.location);
                            std::string where =
                                opt_wloc ? (opt_wloc->file + ":" + std::to_string(opt_wloc->line))
                                         : p;
                            frontend_warnings_.push_back(
                                "Skipped procedural block with unrecoverable clock at " + where);
                            break;
                        }
                        if (!p.empty() && cr.clock.find('.') == std::string::npos &&
                            cr.clock != "unknown") {
                            cr.clock = p + "." + cr.clock;
                        }
                    } else {
                        cr.clock = "comb";
                    }

                    auto opt_loc = to_source_loc(proc.location);
                    ir::SourceLoc ir_loc = opt_loc.value_or(ir::SourceLoc{});

                    std::vector<AssignInfo> assigns;
                    collect_assigns(proc.getBody(), p, assigns);

                    std::unordered_map<std::string, uint64_t> comb_nodes;
                    for (auto& a : assigns) {
                        const ir::Node* dst = graph.find_node_by_name(a.lhs);
                        uint64_t dst_id =
                            dst ? dst->id
                                : (cr.clock == "comb"
                                       ? graph.add_net(a.lhs, a.lhs_width, ir_loc, p, module_type)
                                       : graph.add_register(a.lhs, cr.clock, a.lhs_width, ir_loc, p,
                                                            module_type));
                        if (dst && cr.clock != "comb" && dst->kind != ir::NodeKind::Register) {
                            auto* mutable_dst = graph.find_node_mutable(dst_id);
                            if (mutable_dst) {
                                mutable_dst->kind = ir::NodeKind::Register;
                                mutable_dst->clock_domain = cr.clock;
                                // Preserve earlier info: keep the widest width, the
                                // first-seen location, and OR sticky flags instead of
                                // overwriting them with this driver's values.
                                if (a.lhs_width > mutable_dst->width)
                                    mutable_dst->width = a.lhs_width;
                                if (mutable_dst->loc.file.empty())
                                    mutable_dst->loc = ir_loc;
                                mutable_dst->reset_signal = cr.reset;
                                mutable_dst->reset_pol = cr.reset_pol;
                                mutable_dst->is_async_reset = cr.is_async_reset;
                                mutable_dst->is_gray_coded =
                                    mutable_dst->is_gray_coded || a.is_gray_transform;
                                if (mutable_dst->module_type.empty())
                                    mutable_dst->module_type = module_type;
                                if (allow_user_annotation_)
                                    apply_pattern_name_tags(*mutable_dst);
                            }
                        }
                        if (!dst && cr.clock != "comb") {
                            auto* mutable_dst = graph.find_node_mutable(dst_id);
                            if (mutable_dst) {
                                mutable_dst->reset_signal = cr.reset;
                                mutable_dst->reset_pol = cr.reset_pol;
                                mutable_dst->is_async_reset = cr.is_async_reset;
                                mutable_dst->is_gray_coded = a.is_gray_transform;
                                if (mutable_dst->module_type.empty())
                                    mutable_dst->module_type = module_type;
                                if (allow_user_annotation_)
                                    apply_pattern_name_tags(*mutable_dst);
                            }
                        }

                        // Skip edge creation for literal-only RHS (empty rhs means
                        // no named operand to create an edge from).
                        if (a.rhs.empty())
                            continue;

                        if (a.is_multi_operand && a.logic_type != ir::LogicType::Unknown) {
                            const ir::Node* src = graph.find_node_by_name(a.rhs);
                            uint64_t src_id =
                                src ? src->id
                                    : graph.add_net(a.rhs, a.rhs_width, {}, p, module_type);
                            auto it = comb_nodes.find(a.lhs);
                            if (it == comb_nodes.end()) {
                                std::string comb_name = a.lhs + "$comb";
                                std::vector<uint64_t> comb_inputs = {src_id};
                                ir::LogicType comb_type =
                                    a.is_gray_transform ? ir::LogicType::GrayEncoder : a.logic_type;
                                uint64_t comb_id =
                                    graph.add_combinational(comb_name, comb_type, comb_inputs,
                                                            a.lhs_width, {}, p, module_type);
                                if (comb_id) {
                                    comb_nodes[a.lhs] = comb_id;
                                    graph.add_edge(comb_id, dst_id);
                                }
                            } else {
                                auto* comb_node = graph.find_node_mutable(it->second);
                                if (comb_node) {
                                    comb_node->logic_inputs.push_back(src_id);
                                    graph.add_edge(src_id, it->second);
                                }
                            }
                        } else {
                            const ir::Node* src = graph.find_node_by_name(a.rhs);
                            uint64_t src_id =
                                src ? src->id
                                    : graph.add_net(a.rhs, a.rhs_width, {}, p, module_type);
                            if (src_id && dst_id)
                                graph.add_edge(src_id, dst_id);
                        }
                    }
                }
                break;
            }
            default:
                break;
        }
    }

    for (auto& member : scope.members()) {
        if (member.kind != SymbolKind::ContinuousAssign)
            continue;
        auto& ca = static_cast<const ContinuousAssignSymbol&>(member);
        const Expression& assign_expr = ca.getAssignment();
        if (assign_expr.kind != ExpressionKind::Assignment)
            continue;
        auto& asgn = static_cast<const AssignmentExpression&>(assign_expr);
        auto lhs = extract_lvalue(asgn.left(), p);
        if (!lhs)
            continue;
        std::string lhs_name = lhs->first;

        std::vector<std::pair<std::string, uint32_t>> rhs_ops;
        collect_named_values(asgn.right(), p, rhs_ops);

        ir::LogicType logic_type = ir::LogicType::Unknown;
        if (asgn.right().kind == ExpressionKind::BinaryOp) {
            auto& bin = static_cast<const BinaryExpression&>(asgn.right());
            if (bin.op == BinaryOperator::BinaryAnd)
                logic_type = ir::LogicType::And;
            else if (bin.op == BinaryOperator::BinaryOr)
                logic_type = ir::LogicType::Or;
            else if (bin.op == BinaryOperator::BinaryXor)
                logic_type = ir::LogicType::Xor;
        } else if (asgn.right().kind == ExpressionKind::ConditionalOp) {
            logic_type = ir::LogicType::Mux;
        }

        if (!rhs_ops.empty()) {
            // Prefer the declared LHS width; fall back to the existing node's
            // width only when the extractor yielded nothing useful.
            uint32_t lhs_width = (lhs->second != 0) ? lhs->second : 1;
            const ir::Node* existing_lhs = graph.find_node_by_name(lhs_name);
            if (existing_lhs && existing_lhs->width > lhs_width) {
                lhs_width = existing_lhs->width;
            }
            uint64_t lhs_id = existing_lhs ? existing_lhs->id
                                           : graph.add_net(lhs_name, lhs_width, {}, p, module_type);
            if (!existing_lhs) {
                auto* mutable_lhs = graph.find_node_mutable(lhs_id);
                if (mutable_lhs) {
                    mutable_lhs->logic_type = logic_type;
                }
            }

            if (logic_type != ir::LogicType::Unknown && logic_type != ir::LogicType::None) {
                std::vector<uint64_t> input_ids;
                for (const auto& [rhs_name, rhs_width] : rhs_ops) {
                    const ir::Node* src = graph.find_node_by_name(rhs_name);
                    uint64_t src_id =
                        src ? src->id : graph.add_net(rhs_name, rhs_width, {}, p, module_type);
                    input_ids.push_back(src_id);
                }
                std::string comb_name = lhs_name + "$comb";
                uint64_t comb_id = graph.add_combinational(comb_name, logic_type, input_ids,
                                                           lhs_width, {}, p, module_type);
                if (comb_id)
                    graph.add_edge(comb_id, lhs_id);
            } else {
                for (const auto& [rhs_name, rhs_width] : rhs_ops) {
                    const ir::Node* src = graph.find_node_by_name(rhs_name);
                    uint64_t src_id =
                        src ? src->id : graph.add_net(rhs_name, rhs_width, {}, p, module_type);
                    if (src_id && lhs_id)
                        graph.add_edge(src_id, lhs_id);
                }
            }
        }

        std::string wire_name = lhs_name.substr(p.size() + 1);
        std::string qualified_wire = p + "." + wire_name;

        if (asgn.right().kind == ExpressionKind::ConditionalOp) {
            // Only conditional assigns indicate a muxed clock — mark registers
            // whose clock domain is the LHS wire.
            for (auto& node : graph.nodes_mutable()) {
                if (node.kind == ir::NodeKind::Register &&
                    (node.clock_domain == wire_name || node.clock_domain == qualified_wire)) {
                    node.clock_is_muxed = true;
                }
            }

            for (auto& node : graph.nodes_mutable()) {
                if (node.kind == ir::NodeKind::Register && node.hier_name == lhs_name) {
                    node.clock_is_muxed = true;
                }
            }
        }
    }
    --scope_depth_;
}

void SlangAdapter::resolve_clocks(ir::Graph& graph) {
    clock::ClockResolver resolver;
    auto result = resolver.resolve(graph);

    // Pre-build top-level port sets with a single O(N) pass.
    // A top-level port is a Port node whose hier_name has exactly one '.'.
    std::unordered_set<std::string> top_ports_full;   // full hier_name
    std::unordered_set<std::string> top_ports_short;  // leaf name after last '.'
    for (const auto& node : graph.nodes()) {
        if (node.kind != ir::NodeKind::Port)
            continue;
        size_t dots = std::count(node.hier_name.begin(), node.hier_name.end(), '.');
        if (dots == 1) {
            top_ports_full.insert(node.hier_name);
            size_t dot = node.hier_name.rfind('.');
            top_ports_short.insert(node.hier_name.substr(dot + 1));
        }
    }

    // O(1) predicate using the pre-built sets.
    auto is_top = [&](const std::string& name) -> bool {
        return top_ports_full.count(name) || top_ports_short.count(name);
    };

    for (auto& node : graph.nodes_mutable()) {
        if (node.kind != ir::NodeKind::Register)
            continue;
        if (node.clock_domain.empty() || node.clock_domain == "unknown")
            continue;

        auto it = result.clock_map.find(node.clock_domain);
        if (it != result.clock_map.end()) {
            node.root_clock = it->second.root_clock;
            node.clock_is_gated = it->second.is_gated;
            if (!node.clock_is_muxed) {
                node.clock_is_muxed = it->second.is_muxed;
            }
        }

        if (node.root_clock.empty() || !is_top(node.root_clock)) {
            std::string source = trace_to_top_level_source(node.clock_domain, graph);
            if (!source.empty() && source != node.clock_domain) {
                node.root_clock = source;
            }
        }
    }
}

bool SlangAdapter::is_top_level_input(const std::string& hier_name, const ir::Graph& graph,
                                      const std::unordered_set<std::string>& top_level_ports) {
    return top_level_ports.count(hier_name) > 0;
}

std::string SlangAdapter::trace_to_top_level_source(const std::string& clock_name,
                                                    const ir::Graph& graph) {
    // Build top-level port index once per graph (Port nodes with exactly 1 dot).
    // Keyed by graph identity AND generation: two Graph instances can share a
    // generation number, and mutating a graph bumps its generation.
    struct CacheEntry {
        uint64_t generation = UINT64_MAX;
        size_t node_count = 0;
        std::unordered_set<std::string> ports;
    };
    static thread_local std::unordered_map<const ir::Graph*, CacheEntry> tl_cache;
    if (tl_cache.size() > 16)
        tl_cache.clear();  // bound memory; keys are raw pointers to dead graphs
    CacheEntry& entry = tl_cache[&graph];
    uint64_t cur_gen = graph.generation();
    size_t cur_count = graph.nodes().size();
    if (entry.generation != cur_gen || entry.node_count != cur_count) {
        entry.ports.clear();
        for (const auto& node : graph.nodes()) {
            if (node.kind != ir::NodeKind::Port)
                continue;
            size_t dots = 0;
            for (char c : node.hier_name)
                if (c == '.')
                    dots++;
            if (dots == 1)
                entry.ports.insert(node.hier_name);
        }
        entry.generation = cur_gen;
        entry.node_count = cur_count;
    }
    std::unordered_set<std::string>& tl_top_level_ports = entry.ports;

    const ir::Node* start = graph.find_node_by_name(clock_name);
    if (!start) {
        // Deterministic tie-break: smallest hierarchical name wins instead of
        // graph insertion order, so duplicate leaf names classify identically
        // across runs.
        const ir::Node* best = nullptr;
        for (const auto& node : graph.nodes()) {
            if (node.hier_name == clock_name) {
                best = &node;
                break;  // exact match is unambiguous
            }
            size_t dot = node.hier_name.rfind('.');
            if (dot != std::string::npos && node.hier_name.substr(dot + 1) == clock_name) {
                if (!best || node.hier_name < best->hier_name)
                    best = &node;
            }
        }
        start = best;
    }
    if (!start)
        return clock_name;

    if (start->kind == ir::NodeKind::Port &&
        is_top_level_input(start->hier_name, graph, tl_top_level_ports)) {
        size_t dot = start->hier_name.rfind('.');
        return (dot != std::string::npos) ? start->hier_name.substr(dot + 1) : start->hier_name;
    }

    std::string current = clock_name;
    std::unordered_set<std::string> visited;
    visited.insert(current);

    for (int depth = 0; depth < 20; ++depth) {
        const ir::Node* cn = graph.find_node_by_name(current);
        if (!cn)
            break;

        if (cn->kind == ir::NodeKind::Port &&
            is_top_level_input(cn->hier_name, graph, tl_top_level_ports)) {
            size_t dot = cn->hier_name.rfind('.');
            return (dot != std::string::npos) ? cn->hier_name.substr(dot + 1) : cn->hier_name;
        }

        if (cn->kind == ir::NodeKind::Register) {
            return cn->root_clock.empty() ? cn->clock_domain : cn->root_clock;
        }

        bool found_next = false;
        for (uint64_t pred_id : graph.predecessors(cn->id)) {
            const ir::Node* pred = graph.find_node(pred_id);
            if (!pred)
                continue;

            if (pred->kind == ir::NodeKind::Port) {
                if (is_top_level_input(pred->hier_name, graph, tl_top_level_ports)) {
                    size_t dot = pred->hier_name.rfind('.');
                    return (dot != std::string::npos) ? pred->hier_name.substr(dot + 1)
                                                      : pred->hier_name;
                }
                continue;
            }

            if (pred->kind == ir::NodeKind::Net || pred->kind == ir::NodeKind::Combinational) {
                if (!visited.count(pred->hier_name)) {
                    visited.insert(pred->hier_name);
                    current = pred->hier_name;
                    found_next = true;
                    break;
                }
            }
        }
        if (!found_next)
            break;
    }

    const ir::Node* final_node = graph.find_node_by_name(current);
    if (final_node && final_node->kind == ir::NodeKind::Port) {
        size_t dot = current.rfind('.');
        return (dot != std::string::npos) ? current.substr(dot + 1) : current;
    }

    return clock_name;
}

}  // namespace opencdc::frontend
