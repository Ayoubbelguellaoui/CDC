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
#include "slang/ast/expressions/LiteralExpressions.h"
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
#include "slang/ast/symbols/ParameterSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/SubroutineSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/ast/types/AllTypes.h"
#include "slang/diagnostics/DiagnosticEngine.h"
#include "slang/diagnostics/TextDiagnosticClient.h"
#include "slang/parsing/Preprocessor.h"
#include "slang/syntax/AllSyntax.h"
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
    // Stages of a packed shift chain (`s <= {s[W-1:0], tail}`), 0 when the
    // assignment is not such a chain; applied to the destination node.
    uint32_t packed_depth = 0;
    // rhs is a signal of an enclosing if/case condition: a Control edge.
    bool is_control = false;
};

struct MultiAssignInfo {
    std::string lhs;
    uint32_t lhs_width = 1;
    std::vector<std::pair<std::string, uint32_t>> rhs_operands;
};

static std::string sv_to_string(std::string_view sv) {
    return std::string(sv);
}

// Width of a signal of type `t` in bits. getBitWidth() is zero for every
// non-numeric type - unpacked arrays, unpacked structs and unions - so
// those take their $bits; a type with no static size (dynamic array, queue,
// string) counts as one bit. A graph node never gets width zero.
static uint32_t type_width(const Type& t) {
    uint64_t width = t.getBitWidth();
    if (width == 0)
        width = t.getBitstreamWidth();
    if (width == 0)
        return 1;
    return static_cast<uint32_t>(std::min<uint64_t>(width, UINT32_MAX));
}

// SlangAdapter::scope_paths_ while elaborate() walks the design, for the
// free functions below that name signals.
static thread_local const std::unordered_map<const Scope*, std::string>* g_scope_paths = nullptr;

// Hierarchical name of a signal: the path of the scope that declares it, or
// `prefix` for a scope the walk did not enter (subroutine and statement-block
// locals, packages). A generate block reads its module's signals, so naming
// them after the block would split one net into several nodes.
static std::string symbol_path(const Symbol& sym, const std::string& prefix) {
    if (g_scope_paths) {
        if (const Scope* scope = sym.getParentScope()) {
            auto it = g_scope_paths->find(scope);
            if (it != g_scope_paths->end())
                return it->second + "." + sv_to_string(sym.name);
        }
    }
    return prefix + "." + sv_to_string(sym.name);
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

// Value of the variable of each procedural for loop being unrolled
// (collect_assigns, ArrayAccessScan): an index of the body is static.
static thread_local std::unordered_map<const Symbol*, int64_t> g_loop_values;

// Index of an element select known at elaboration: a literal, a parameter
// (a genvar is a parameter in each generate-loop entry), or + - * of those.
static std::optional<int64_t> static_index(const Expression& expr) {
    const Expression* e = &expr;
    while (e->kind == ExpressionKind::Conversion)
        e = &static_cast<const ConversionExpression&>(*e).operand();
    if (e->kind == ExpressionKind::IntegerLiteral) {
        SVInt v = static_cast<const IntegerLiteral&>(*e).getValue();
        if (v.hasUnknown())
            return std::nullopt;
        return v.as<int64_t>();
    }
    if (e->kind == ExpressionKind::NamedValue) {
        auto& sym = static_cast<const NamedValueExpression&>(*e).symbol;
        if (auto it = g_loop_values.find(&sym); it != g_loop_values.end())
            return it->second;
        if (sym.kind != SymbolKind::Parameter)
            return std::nullopt;
        const ConstantValue* cv = &static_cast<const ParameterSymbol&>(sym).getValue();
        if (!cv->isInteger() || cv->integer().hasUnknown())
            return std::nullopt;
        return cv->integer().as<int64_t>();
    }
    if (e->kind == ExpressionKind::BinaryOp) {
        auto& bin = static_cast<const BinaryExpression&>(*e);
        auto l = static_index(bin.left());
        auto r = static_index(bin.right());
        if (!l || !r)
            return std::nullopt;
        switch (bin.op) {
            case BinaryOperator::Add:
                return *l + *r;
            case BinaryOperator::Subtract:
                return *l - *r;
            case BinaryOperator::Multiply:
                return *l * *r;
            default:
                return std::nullopt;
        }
    }
    return std::nullopt;
}

// Iteration values of a procedural loop `for (int i = A; i < B; i++)`:
// one variable, static A and B, a < <= > >= != stop, ++ -- += -= of a
// static step; at most 1024 iterations. nullopt for any other loop.
struct LoopUnroll {
    const Symbol* var = nullptr;
    std::vector<int64_t> values;
};

static std::optional<LoopUnroll> unroll_for(const ForLoopStatement& fl) {
    const Symbol* var = nullptr;
    std::optional<int64_t> init;
    if (fl.loopVars.size() == 1 && fl.loopVars[0]->getInitializer()) {
        var = fl.loopVars[0];
        init = static_index(*fl.loopVars[0]->getInitializer());
    } else if (fl.loopVars.empty() && fl.initializers.size() == 1 &&
               fl.initializers[0]->kind == ExpressionKind::Assignment) {
        auto& a = static_cast<const AssignmentExpression&>(*fl.initializers[0]);
        if (!a.isCompound() && a.left().kind == ExpressionKind::NamedValue) {
            var = &static_cast<const NamedValueExpression&>(a.left()).symbol;
            init = static_index(a.right());
        }
    }
    if (!var || !init || !fl.stopExpr || fl.steps.size() != 1 ||
        fl.stopExpr->kind != ExpressionKind::BinaryOp)
        return std::nullopt;
    auto is_var = [&](const Expression& e) {
        const Expression* x = &e;
        while (x->kind == ExpressionKind::Conversion)
            x = &static_cast<const ConversionExpression&>(*x).operand();
        return x->kind == ExpressionKind::NamedValue &&
               &static_cast<const NamedValueExpression&>(*x).symbol == var;
    };
    auto& stop = static_cast<const BinaryExpression&>(*fl.stopExpr);
    if (!is_var(stop.left()))
        return std::nullopt;
    auto bound = static_index(stop.right());
    if (!bound)
        return std::nullopt;

    int64_t step = 0;
    const Expression& st = *fl.steps[0];
    if (st.kind == ExpressionKind::UnaryOp) {
        auto& u = static_cast<const UnaryExpression&>(st);
        if (!is_var(u.operand()))
            return std::nullopt;
        if (u.op == UnaryOperator::Preincrement || u.op == UnaryOperator::Postincrement)
            step = 1;
        else if (u.op == UnaryOperator::Predecrement || u.op == UnaryOperator::Postdecrement)
            step = -1;
    } else if (st.kind == ExpressionKind::Assignment) {
        auto& a = static_cast<const AssignmentExpression&>(st);
        auto k = static_index(a.right());
        if (a.isCompound() && is_var(a.left()) && k) {
            if (*a.op == BinaryOperator::Add)
                step = *k;
            else if (*a.op == BinaryOperator::Subtract)
                step = -*k;
        }
    }
    if (step == 0)
        return std::nullopt;

    LoopUnroll out;
    out.var = var;
    for (int64_t v = *init; out.values.size() <= 1024; v += step) {
        bool go = false;
        switch (stop.op) {
            case BinaryOperator::LessThan:
                go = v < *bound;
                break;
            case BinaryOperator::LessThanEqual:
                go = v <= *bound;
                break;
            case BinaryOperator::GreaterThan:
                go = v > *bound;
                break;
            case BinaryOperator::GreaterThanEqual:
                go = v >= *bound;
                break;
            case BinaryOperator::Inequality:
                go = v != *bound;
                break;
            default:
                return std::nullopt;
        }
        if (!go)
            return out;
        out.values.push_back(v);
    }
    return std::nullopt;  // more than 1024 iterations
}

// The fixed-size unpacked array variable or net a named value refers to.
static const Symbol* unpacked_array_symbol(const Expression& expr) {
    if (expr.kind != ExpressionKind::NamedValue)
        return nullptr;
    auto& sym = static_cast<const NamedValueExpression&>(expr).symbol;
    if (sym.kind != SymbolKind::Variable && sym.kind != SymbolKind::Net)
        return nullptr;
    const Type& t = static_cast<const ValueSymbol&>(sym).getType();
    return t.isUnpackedArray() && t.isFixedSize() ? &sym : nullptr;
}

// Unpacked arrays whose every access is an element select with a static
// index (SlangAdapter::split_arrays_ while elaborate() walks the design):
// each element is a node of its own, `s[0]`, `s[1]`, so a synchronizer
// written as an array is a chain. Any other array is one node.
static thread_local const std::unordered_set<const Symbol*>* g_split_arrays = nullptr;

// Node name of the element select `value[selector]` when value is a split
// array, else nullopt.
static std::optional<std::string> split_element_name(const ElementSelectExpression& es,
                                                     const std::string& prefix) {
    const Symbol* sym = unpacked_array_symbol(es.value());
    if (!sym || !g_split_arrays || !g_split_arrays->count(sym))
        return std::nullopt;
    auto index = static_index(es.selector());
    if (!index)
        return std::nullopt;
    return symbol_path(*sym, prefix) + "[" + std::to_string(*index) + "]";
}

// Finds the unpacked arrays to split: those accessed only through element
// selects with a static index. A bare reference (whole-array assignment,
// port connection, variable index) keeps the array one node.
struct ArrayAccessScan : public ASTVisitor<ArrayAccessScan, true, true> {
    std::unordered_set<const Symbol*> indexed;
    std::unordered_set<const Symbol*> whole;

    void handle(const ElementSelectExpression& es) {
        if (unpacked_array_symbol(es.value()) && static_index(es.selector())) {
            indexed.insert(unpacked_array_symbol(es.value()));
            es.selector().visit(*this);
            return;
        }
        visitDefault(es);
    }
    void handle(const NamedValueExpression& nv) {
        if (const Symbol* sym = unpacked_array_symbol(nv))
            whole.insert(sym);
    }
    void handle(const ForLoopStatement& fl) {
        // An index by the variable of an unrollable loop is static.
        auto unroll = unroll_for(fl);
        if (!unroll || unroll->values.empty()) {
            visitDefault(fl);
            return;
        }
        g_loop_values[unroll->var] = unroll->values.front();
        visitDefault(fl);
        g_loop_values.erase(unroll->var);
    }
    void handle(const InstanceSymbol& inst) {
        for (const auto* connection : inst.getPortConnections()) {
            if (connection && connection->getExpression())
                connection->getExpression()->visit(*this);
        }
        visitDefault(inst);
    }

    std::unordered_set<const Symbol*> split() const {
        std::unordered_set<const Symbol*> out;
        for (const Symbol* sym : indexed) {
            if (!whole.count(sym))
                out.insert(sym);
        }
        return out;
    }
};

static std::optional<std::pair<std::string, uint32_t>> extract_lvalue(const Expression& expr,
                                                                      const std::string& prefix) {
    if (expr.kind == ExpressionKind::NamedValue) {
        auto& nv = static_cast<const NamedValueExpression&>(expr);
        return std::make_pair(symbol_path(nv.symbol, prefix), type_width(*expr.type));
    }
    if (expr.kind == ExpressionKind::MemberAccess) {
        auto& member = static_cast<const MemberAccessExpression&>(expr);
        auto base = extract_lvalue(member.value(), prefix);
        if (!base)
            return std::nullopt;
        base->first += "." + sv_to_string(member.member.name);
        base->second = type_width(*expr.type);
        return base;
    }
    if (expr.kind == ExpressionKind::ElementSelect) {
        auto& select = static_cast<const ElementSelectExpression&>(expr);
        if (auto element = split_element_name(select, prefix))
            return std::make_pair(*element, type_width(*expr.type));
        auto base = extract_lvalue(select.value(), prefix);
        if (!base)
            return std::nullopt;
        base->second = type_width(*expr.type);
        return base;
    }
    if (expr.kind == ExpressionKind::RangeSelect) {
        auto& select = static_cast<const RangeSelectExpression&>(expr);
        auto base = extract_lvalue(select.value(), prefix);
        if (!base)
            return std::nullopt;
        base->second = type_width(*expr.type);
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
        // Parameters (genvars included) and enum values are constants of
        // the elaboration, not signals: no node, no edge.
        if (nv.symbol.kind == SymbolKind::Parameter || nv.symbol.kind == SymbolKind::EnumValue)
            return;
        std::string raw_name = sv_to_string(nv.symbol.name);
        auto fm_it = formal_map.find(raw_name);
        std::string name;
        uint32_t width = 1;
        if (fm_it != formal_map.end()) {
            name = fm_it->second.first;
            width = fm_it->second.second;
        } else {
            name = symbol_path(nv.symbol, prefix);
            if (nv.symbol.kind == SymbolKind::Variable) {
                auto& var = static_cast<const VariableSymbol&>(nv.symbol);
                width = type_width(var.getType());
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
        if (auto element = split_element_name(es, prefix)) {
            out.push_back({*element, type_width(*expr.type)});
            return;
        }
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
static void collect_bitwise_gray(const Statement& stmt, const Symbol& var, const Symbol*& formal,
                                 std::vector<bool>& ok, size_t depth);

// The formal a function's local `var` is the gray code of, when the body
// builds it bit by bit: `g[k] = b[k+1] ^ b[k]` for every k < W-1 and
// `g[W-1] = b[W-1]` (loops unrolled), over one formal b. Else null.
static const Symbol* bitwise_gray_formal(const SubroutineSymbol& sub, const Symbol& var) {
    if (var.kind != SymbolKind::Variable)
        return nullptr;
    uint32_t width = static_cast<const VariableSymbol&>(var).getType().getBitWidth();
    if (width < 2 || width > 1024)
        return nullptr;
    const Symbol* formal = nullptr;
    std::vector<bool> ok(width, false);
    collect_bitwise_gray(sub.getBody(), var, formal, ok, 0);
    if (!formal)
        return nullptr;
    for (bool b : ok) {
        if (!b)
            return nullptr;
    }
    for (const auto* f : sub.getArguments()) {
        if (f == formal)
            return formal;
    }
    return nullptr;
}

// Records into `ok` the bits of `var` the statement assigns as gray bits.
static void collect_bitwise_gray(const Statement& stmt, const Symbol& var, const Symbol*& formal,
                                 std::vector<bool>& ok, size_t depth) {
    if (depth > 16)
        return;
    switch (stmt.kind) {
        case StatementKind::Block:
            collect_bitwise_gray(static_cast<const BlockStatement&>(stmt).body, var, formal, ok,
                                 depth + 1);
            return;
        case StatementKind::List:
            for (auto* st : static_cast<const StatementList&>(stmt).list)
                collect_bitwise_gray(*st, var, formal, ok, depth + 1);
            return;
        case StatementKind::ForLoop: {
            auto& fl = static_cast<const ForLoopStatement&>(stmt);
            auto unroll = unroll_for(fl);
            if (!unroll)
                return;
            for (int64_t v : unroll->values) {
                g_loop_values[unroll->var] = v;
                collect_bitwise_gray(fl.body, var, formal, ok, depth + 1);
            }
            g_loop_values.erase(unroll->var);
            return;
        }
        case StatementKind::ExpressionStatement:
            break;
        default:
            return;
    }
    auto& es = static_cast<const ExpressionStatement&>(stmt);
    if (es.expr.kind != ExpressionKind::Assignment)
        return;
    auto& asgn = static_cast<const AssignmentExpression&>(es.expr);
    if (asgn.isCompound() || asgn.left().kind != ExpressionKind::ElementSelect)
        return;
    auto& lhs = static_cast<const ElementSelectExpression&>(asgn.left());
    if (lhs.value().kind != ExpressionKind::NamedValue ||
        &static_cast<const NamedValueExpression&>(lhs.value()).symbol != &var)
        return;
    auto k = static_index(lhs.selector());
    if (!k || *k < 0 || *k >= static_cast<int64_t>(ok.size()))
        return;
    // The bit b[i] of a formal: its symbol and index.
    auto formal_bit = [](const Expression& e) -> std::optional<std::pair<const Symbol*, int64_t>> {
        const Expression* x = &e;
        while (x->kind == ExpressionKind::Conversion)
            x = &static_cast<const ConversionExpression&>(*x).operand();
        if (x->kind != ExpressionKind::ElementSelect)
            return std::nullopt;
        auto& sel = static_cast<const ElementSelectExpression&>(*x);
        if (sel.value().kind != ExpressionKind::NamedValue)
            return std::nullopt;
        auto i = static_index(sel.selector());
        if (!i)
            return std::nullopt;
        return std::make_pair(&static_cast<const NamedValueExpression&>(sel.value()).symbol, *i);
    };
    auto use = [&](const Symbol* f) {
        if (!formal)
            formal = f;
        return formal == f;
    };
    const Expression& rhs = asgn.right();
    if (*k == static_cast<int64_t>(ok.size()) - 1) {
        auto bit = formal_bit(rhs);
        if (bit && bit->second == *k && use(bit->first))
            ok[*k] = true;
        return;
    }
    if (rhs.kind != ExpressionKind::BinaryOp)
        return;
    auto& bin = static_cast<const BinaryExpression&>(rhs);
    if (bin.op != BinaryOperator::BinaryXor)
        return;
    auto l = formal_bit(bin.left());
    auto r = formal_bit(bin.right());
    if (!l || !r || l->first != r->first)
        return;
    bool adjacent =
        (l->second == *k && r->second == *k + 1) || (l->second == *k + 1 && r->second == *k);
    if (adjacent && use(l->first))
        ok[*k] = true;
}

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
        } else if (cand->kind == ExpressionKind::NamedValue) {
            // `return g;` of a local built bit by bit (bitwise_gray_formal).
            auto& sym = static_cast<const NamedValueExpression&>(*cand).symbol;
            const Symbol* shared = bitwise_gray_formal(*sub, sym);
            if (shared && local.find(sv_to_string(shared->name)) != local.end())
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

// Constant integer value of a select bound. Only literal (possibly
// conversion-wrapped) expressions are accepted; anything else — parameter
// references, arithmetic — yields nullopt so callers stay conservative.
static std::optional<uint64_t> const_int_value(const Expression& expr) {
    const Expression* e = &expr;
    while (e->kind == ExpressionKind::Conversion)
        e = &static_cast<const ConversionExpression&>(*e).operand();
    if (e->kind != ExpressionKind::IntegerLiteral)
        return std::nullopt;
    auto& lit = static_cast<const IntegerLiteral&>(*e);
    if (lit.getValue().hasUnknown())
        return std::nullopt;
    return lit.getValue().as<uint64_t>();
}

// The constant [hi:lo] range an operand selects from lhs, or false when the
// operand is not an element/range select of lhs with literal bounds (any
// other shape is treated conservatively as "not a verified shift step").
static bool lhs_select_range(const Expression& op, const std::string& lhs_name,
                             const std::string& prefix, uint32_t& hi, uint32_t& lo) {
    const Expression* base = nullptr;
    if (op.kind == ExpressionKind::ElementSelect) {
        auto& es = static_cast<const ElementSelectExpression&>(op);
        auto idx = const_int_value(es.selector());
        if (!idx || *idx > 0xFFFFFFFFull)
            return false;
        hi = lo = static_cast<uint32_t>(*idx);
        base = &es.value();
    } else if (op.kind == ExpressionKind::RangeSelect) {
        auto& rs = static_cast<const RangeSelectExpression&>(op);
        if (rs.getSelectionKind() != RangeSelectionKind::Simple)
            return false;
        auto left = const_int_value(rs.left());
        auto right = const_int_value(rs.right());
        if (!left || !right || *left > 0xFFFFFFFFull || *right > 0xFFFFFFFFull || *left < *right)
            return false;
        hi = static_cast<uint32_t>(*left);
        lo = static_cast<uint32_t>(*right);
        base = &rs.value();
    } else {
        return false;
    }
    if (base->kind != ExpressionKind::NamedValue)
        return false;
    auto& nv = static_cast<const NamedValueExpression&>(*base);
    return symbol_path(nv.symbol, prefix) == lhs_name;
}

// Stage count of a packed shift chain `lhs <= {lhs_hi..., tail}`: the tail
// (last operand, resident in the low lhs bits) is the ingress — e.g. the
// cross-domain source of a vector-packed synchronizer — and every other
// operand must be the lhs range below its own target position shifted up by
// the tail width, so values flow strictly upward through the vector with no
// shortcuts. `sync <= {sync[0], src}` is the canonical two-stage form; the
// tail must be a signal, not logic. Concatenation operands are ordered MSB-first. Returns
// floor(lhs_width / tail_width) when that is >= 2 stages, otherwise 0 (not a chain).
static uint32_t packed_shift_chain_stages(const Expression& rhs, const std::string& lhs_name,
                                          const std::string& prefix, uint32_t lhs_width) {
    if (rhs.kind != ExpressionKind::Concatenation || lhs_width < 2)
        return 0;
    auto& cat = static_cast<const ConcatenationExpression&>(rhs);
    auto ops = cat.operands();
    if (ops.size() < 2)
        return 0;

    uint32_t tail_width = ops.back()->type ? (uint32_t)ops.back()->type->getBitWidth() : 0;
    if (tail_width == 0 || tail_width >= lhs_width)
        return 0;
    // The ingress is a signal (or a select of one), as the first stage of a
    // scalar chain reads one: logic in front of the synchronizer (`{s[0],
    // a & b}`) can glitch into it, so that is no synchronizer chain.
    const Expression* tail = ops.back();
    while (tail->kind == ExpressionKind::Conversion)
        tail = &static_cast<const ConversionExpression&>(*tail).operand();
    while (tail->kind == ExpressionKind::ElementSelect || tail->kind == ExpressionKind::RangeSelect)
        tail = tail->kind == ExpressionKind::ElementSelect
                   ? &static_cast<const ElementSelectExpression&>(*tail).value()
                   : &static_cast<const RangeSelectExpression&>(*tail).value();
    if (tail->kind != ExpressionKind::NamedValue)
        return 0;

    // Each upper operand must select from lhs at exactly its target range
    // shifted down by tail_width: target bit k is fed by lhs bit k - Bw.
    uint32_t from_top = 0;
    for (size_t i = 0; i + 1 < ops.size(); ++i) {
        const Expression& op = *ops[i];
        uint32_t w = op.type ? (uint32_t)op.type->getBitWidth() : 0;
        if (w == 0 || from_top + w > lhs_width)
            return 0;
        uint32_t tgt_hi = lhs_width - 1 - from_top;
        uint32_t tgt_lo = tgt_hi + 1 - w;
        if (tgt_hi < tail_width || tgt_lo < tail_width)
            return 0;
        uint32_t src_hi = 0;
        uint32_t src_lo = 0;
        if (!lhs_select_range(op, lhs_name, prefix, src_hi, src_lo))
            return 0;
        if (src_hi != tgt_hi - tail_width || src_lo != tgt_lo - tail_width)
            return 0;
        from_top += w;
    }
    // The upper group must cover the whole vector above the tail — no
    // padding, truncation, constants, or foreign signals mixed in.
    if (lhs_width - from_top != tail_width)
        return 0;
    uint32_t stages = lhs_width / tail_width;
    return stages >= 2 ? stages : 0;
}

// The targets of an assignment to a concatenation `{a, b[3:0], c} = rhs`
// (nested concatenations flattened), each with the right-hand side that
// drives it: the element of a right-hand side concatenation of the same
// shape (`{s1, s0} <= {s0, d}` is a shift), else the whole right-hand side.
// Empty when an element is no lvalue extract_lvalue can name.
struct ConcatTarget {
    std::pair<std::string, uint32_t> lvalue;
    const Expression* rhs;
};

static void flatten_concat(const Expression& e, std::vector<const Expression*>& out) {
    if (e.kind == ExpressionKind::Concatenation) {
        for (const auto* op : static_cast<const ConcatenationExpression&>(e).operands())
            flatten_concat(*op, out);
    } else {
        out.push_back(&e);
    }
}

static std::vector<ConcatTarget> split_concat_assignment(const Expression& lhs,
                                                         const Expression& rhs,
                                                         const std::string& prefix) {
    std::vector<const Expression*> lhs_ops, rhs_ops;
    flatten_concat(lhs, lhs_ops);
    if (rhs.kind == ExpressionKind::Concatenation)
        flatten_concat(rhs, rhs_ops);
    bool paired = rhs_ops.size() == lhs_ops.size();
    for (size_t i = 0; paired && i < lhs_ops.size(); ++i)
        paired = type_width(*lhs_ops[i]->type) == type_width(*rhs_ops[i]->type);
    std::vector<ConcatTarget> out;
    for (size_t i = 0; i < lhs_ops.size(); ++i) {
        auto lv = extract_lvalue(*lhs_ops[i], prefix);
        if (!lv)
            return {};
        out.push_back({*lv, paired ? rhs_ops[i] : &rhs});
    }
    return out;
}

// Signals of the if/case conditions enclosing the statement collect_assigns
// is in: every assignment depends on them as much as on its operands (an
// asynchronous flag used as an enable is a crossing). The block's own
// asynchronous reset is left out - that is the reset-domain check.
static thread_local std::vector<std::pair<std::string, uint32_t>> g_control_operands;
static thread_local const Symbol* g_async_reset_symbol = nullptr;

// Pushes the signals of `cond` onto g_control_operands; returns how many.
static size_t push_control_operands(const Expression& cond, const std::string& prefix,
                                    const FormalMap& formal_map) {
    std::vector<std::pair<std::string, uint32_t>> vals;
    collect_named_values(cond, prefix, vals, formal_map);
    std::string reset =
        g_async_reset_symbol ? symbol_path(*g_async_reset_symbol, prefix) : std::string();
    size_t n = 0;
    for (auto& v : vals) {
        if (v.first == reset)
            continue;
        g_control_operands.push_back(std::move(v));
        ++n;
    }
    return n;
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
            if (es.expr.kind == ExpressionKind::Assignment &&
                static_cast<const AssignmentExpression&>(es.expr).left().kind ==
                    ExpressionKind::Concatenation) {
                // `{a, b} <= ...`: one assignment per element, each a direct
                // dependency (no logic node) on its own right-hand side.
                auto& assign = static_cast<const AssignmentExpression&>(es.expr);
                for (const auto& t :
                     split_concat_assignment(assign.left(), assign.right(), prefix)) {
                    std::vector<std::pair<std::string, uint32_t>> ops;
                    collect_named_values(*t.rhs, prefix, ops, formal_map);
                    if (ops.empty())
                        out.push_back({t.lvalue.first, std::string(), t.lvalue.second, 0, false,
                                       ir::LogicType::Unknown, false, 0});
                    for (const auto& [r, r_width] : ops)
                        out.push_back({t.lvalue.first, r, t.lvalue.second, r_width, false,
                                       ir::LogicType::Unknown, false, 0});
                    for (const auto& [c, c_width] : g_control_operands)
                        out.push_back({t.lvalue.first, c, t.lvalue.second, c_width, false,
                                       ir::LogicType::Unknown, false, 0, true});
                }
            } else if (es.expr.kind == ExpressionKind::Assignment) {
                auto& assign = static_cast<const AssignmentExpression&>(es.expr);
                auto lhs = extract_lvalue(assign.left(), prefix);
                if (lhs) {
                    // Formals are keyed by their bare name; a signal of an
                    // enclosing scope (named by symbol_path) is no formal.
                    uint32_t l_width = lhs->second;
                    std::string resolved_lhs = lhs->first;
                    if (lhs->first.compare(0, prefix.size() + 1, prefix + ".") == 0) {
                        auto fm_it = formal_map.find(lhs->first.substr(prefix.size() + 1));
                        if (fm_it != formal_map.end()) {
                            resolved_lhs = fm_it->second.first;
                            l_width = fm_it->second.second;
                        }
                    }
                    std::vector<std::pair<std::string, uint32_t>> rhs_ops;
                    collect_named_values(assign.right(), prefix, rhs_ops, formal_map);
                    bool is_gray_transform = false;
                    ir::LogicType logic_type = ir::LogicType::Unknown;
                    bool is_multi_operand = false;
                    // Verify a pure intra-vector shift chain on the AST
                    // (`sync <= {sync[0], src}`) before the operands are
                    // flattened: at graph level it is indistinguishable from
                    // `sync <= sync | src`.
                    uint32_t packed_depth =
                        packed_shift_chain_stages(assign.right(), resolved_lhs, prefix, l_width);
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
                                       logic_type, false, packed_depth});
                    } else if (rhs_ops.size() == 1 && !is_multi_operand) {
                        out.push_back({resolved_lhs, rhs_ops[0].first, l_width, rhs_ops[0].second,
                                       is_gray_transform, logic_type, false, packed_depth});
                    } else if (rhs_ops.size() >= 1) {
                        for (auto& [r, r_width] : rhs_ops) {
                            out.push_back({resolved_lhs, r, l_width, r_width, is_gray_transform,
                                           logic_type, true, packed_depth});
                        }
                    }
                    // Control dependencies: a direct edge each, beside the
                    // data path (merged into it, an enable would hide a
                    // synchronizer chain behind logic).
                    for (const auto& [c, c_width] : g_control_operands) {
                        bool data = false;
                        for (const auto& op : rhs_ops)
                            data = data || op.first == c;
                        if (!data && c != resolved_lhs)
                            out.push_back({resolved_lhs, c, l_width, c_width, false,
                                           ir::LogicType::Unknown, false, 0, true});
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
            size_t pushed = 0;
            for (const auto& cond : ifst.conditions)
                pushed += push_control_operands(*cond.expr, prefix, formal_map);
            collect_assigns(ifst.ifTrue, prefix, out, formal_map, depth);
            if (ifst.ifFalse)
                collect_assigns(*ifst.ifFalse, prefix, out, formal_map, depth);
            g_control_operands.resize(g_control_operands.size() - pushed);
            break;
        }
        case StatementKind::ForLoop: {
            // Unrolled when the bounds are static: each iteration's
            // shift_reg[i] <= shift_reg[i-1] names its elements.
            auto& fl = static_cast<const ForLoopStatement&>(stmt);
            auto unroll = unroll_for(fl);
            if (!unroll) {
                collect_assigns(fl.body, prefix, out, formal_map, depth);
                break;
            }
            for (int64_t v : unroll->values) {
                g_loop_values[unroll->var] = v;
                collect_assigns(fl.body, prefix, out, formal_map, depth);
            }
            g_loop_values.erase(unroll->var);
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
            size_t pushed = push_control_operands(cs.expr, prefix, formal_map);
            for (auto& item : cs.items)
                collect_assigns(*item.stmt, prefix, out, formal_map, depth);
            if (cs.defaultCase)
                collect_assigns(*cs.defaultCase, prefix, out, formal_map, depth);
            g_control_operands.resize(g_control_operands.size() - pushed);
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

// `always @*` / `always @(a or b)`: no edge in the event control, so the
// block is combinational logic, like always_comb (it had no clock and was
// skipped, dropping the logic).
static bool is_level_sensitive(const ProceduralBlockSymbol& proc) {
    const Statement& body = proc.getBody();
    if (body.kind != StatementKind::Timed)
        return false;
    const TimingControl& t = static_cast<const TimedStatement&>(body).timing;
    if (t.kind == TimingControlKind::ImplicitEvent)
        return true;
    if (t.kind == TimingControlKind::SignalEvent)
        return static_cast<const SignalEventControl&>(t).edge == EdgeKind::None;
    if (t.kind == TimingControlKind::EventList) {
        for (const auto* ev : static_cast<const EventListControl&>(t).events) {
            if (!ev || ev->kind != TimingControlKind::SignalEvent ||
                static_cast<const SignalEventControl&>(*ev).edge != EdgeKind::None)
                return false;
        }
        return true;
    }
    return false;
}

// The declaration of a signal event that is a plain named value, else null.
static const Symbol* event_symbol(const TimingControl& event) {
    if (event.kind != TimingControlKind::SignalEvent)
        return nullptr;
    auto& se = static_cast<const SignalEventControl&>(event);
    if (se.expr.kind != ExpressionKind::NamedValue)
        return nullptr;
    return &static_cast<const NamedValueExpression&>(se.expr).symbol;
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

// The signals the leading `if` of a clocked block's body tests: in
// `@(posedge clk or negedge rst_n) if (!rst_n) ...` the asynchronous control
// is the event signal tested there (synthesis semantics), the clock the one
// that is not.
static void collect_symbols(const Expression& e, std::unordered_set<const Symbol*>& out) {
    switch (e.kind) {
        case ExpressionKind::NamedValue:
            out.insert(&static_cast<const NamedValueExpression&>(e).symbol);
            break;
        case ExpressionKind::Conversion:
            collect_symbols(static_cast<const ConversionExpression&>(e).operand(), out);
            break;
        case ExpressionKind::UnaryOp:
            collect_symbols(static_cast<const UnaryExpression&>(e).operand(), out);
            break;
        case ExpressionKind::BinaryOp: {
            auto& b = static_cast<const BinaryExpression&>(e);
            collect_symbols(b.left(), out);
            collect_symbols(b.right(), out);
            break;
        }
        default:
            break;
    }
}

static std::unordered_set<const Symbol*> leading_if_symbols(const Statement& stmt) {
    std::unordered_set<const Symbol*> out;
    const Statement* s = &stmt;
    for (int depth = 0; depth < 8; ++depth) {
        if (s->kind == StatementKind::Block) {
            s = &static_cast<const BlockStatement&>(*s).body;
        } else if (s->kind == StatementKind::List) {
            // The first statement after block-local declarations.
            const Statement* first = nullptr;
            for (const auto* st : static_cast<const StatementList&>(*s).list) {
                if (st->kind != StatementKind::VariableDeclaration) {
                    first = st;
                    break;
                }
            }
            if (!first)
                return out;
            s = first;
        } else {
            break;
        }
    }
    if (s->kind != StatementKind::Conditional)
        return out;
    for (const auto& cond : static_cast<const ConditionalStatement&>(*s).conditions)
        collect_symbols(*cond.expr, out);
    return out;
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
        if (!name.empty()) {
            info.clock = name;
            info.clock_symbol = event_symbol(timing);
        }
    } else if (timing.kind == TimingControlKind::EventList) {
        auto& el = static_cast<const EventListControl&>(timing);
        // By structure: the edge events the leading if tests are the
        // asynchronous controls; exactly one untested edge event is the
        // clock. Names decide only when the structure does not (a reset
        // named `trst_ni` or `IwAsyncRstIn_an` was taken for the clock).
        {
            auto tested = leading_if_symbols(timed.stmt);
            const TimingControl* clock_ev = nullptr;
            const SignalEventControl* reset_ev = nullptr;
            size_t untested = 0;
            for (auto& ev : el.events) {
                if (!ev || ev->kind != TimingControlKind::SignalEvent)
                    continue;
                auto& se = static_cast<const SignalEventControl&>(*ev);
                if (se.edge != EdgeKind::NegEdge && se.edge != EdgeKind::PosEdge)
                    continue;
                const Symbol* sym = event_symbol(*ev);
                if (sym && tested.count(sym)) {
                    if (!reset_ev)
                        reset_ev = &se;
                } else {
                    clock_ev = ev;
                    ++untested;
                }
            }
            if (reset_ev && untested == 1) {
                std::string clock = event_signal_name(*clock_ev);
                std::string reset = event_signal_name(*reset_ev);
                if (!clock.empty() && !reset.empty()) {
                    info.clock = clock;
                    info.clock_symbol = event_symbol(*clock_ev);
                    info.reset = reset;
                    info.reset_symbol = event_symbol(*reset_ev);
                    info.reset_pol = (reset_ev->edge == EdgeKind::NegEdge)
                                         ? ir::ResetPolarity::ActiveLow
                                         : ir::ResetPolarity::ActiveHigh;
                    info.is_async_reset = true;
                    return info;
                }
            }
        }
        // Classify each event by role (reset vs clock) instead of position.
        // The clock is the last non-reset edge event, matching the Verilog
        // convention `@(posedge reset or posedge clk)`.
        std::string clock_candidate;
        const Symbol* clock_candidate_symbol = nullptr;
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
                info.reset_symbol = event_symbol(*ev);
                info.reset_pol = (se.edge == EdgeKind::NegEdge) ? ir::ResetPolarity::ActiveLow
                                                                : ir::ResetPolarity::ActiveHigh;
                info.is_async_reset = true;
            } else {
                clock_candidate = name;
                clock_candidate_symbol = event_symbol(*ev);
            }
        }
        if (!clock_candidate.empty()) {
            info.clock = clock_candidate;
            info.clock_symbol = clock_candidate_symbol;
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
                    info.clock_symbol = event_symbol(*ev);
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
    result.input_type = "rtl";
    allow_user_annotation_ = options.allow_user_annotation;
    muxed_clock_domain_ = options.muxed_clock_domain;
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

    std::vector<std::shared_ptr<SyntaxTree>> trees;
    bool top_declared = false;
    for (const auto& file : files) {
        auto treeOrErr = SyntaxTree::fromFile(file, *source_manager_, bag);
        if (!treeOrErr) {
            result.errors.push_back("Failed to read: " + file);
            result.ok = false;
            return result;
        }
        const auto& unit = (*treeOrErr)->root();
        if (unit.kind == SyntaxKind::CompilationUnit) {
            for (const auto* member : unit.as<CompilationUnitSyntax>().members) {
                if (member->kind == SyntaxKind::ModuleDeclaration &&
                    member->as<ModuleDeclarationSyntax>().header->name.valueText() == top_module)
                    top_declared = true;
            }
        }
        trees.push_back(*treeOrErr);
    }
    // Elaborate the requested top only: left to choose, slang takes every
    // uninstantiated module for a top and elaborates library modules with
    // their default parameters - for type-parameterized IP (PULP AXI) that
    // is a compile error in a correct design. An unknown top keeps slang's
    // choice, so the "not found" hint below lists the modules.
    CompilationOptions copts;
    if (top_declared)
        copts.topModules.emplace(top_module);
    // A signal used above its declaration: illegal in strict LRM terms, but
    // accepted by the simulators and synthesis tools designs are built
    // with (Verilator, VCS, Xcelium); the meaning is the same either way.
    copts.flags |= CompilationFlags::AllowUseBeforeDeclare;
    bag.set(copts);
    Compilation compilation(bag);
    for (const auto& tree : trees)
        compilation.addSyntaxTree(tree);

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

    ArrayAccessScan scan;
    top_inst->visit(scan);
    split_arrays_ = scan.split();
    g_split_arrays = &split_arrays_;
    scope_paths_.clear();
    reset_paths_.clear();
    g_scope_paths = &scope_paths_;
    walk_instance(*top_inst, result.graph, "");
    g_scope_paths = nullptr;
    g_split_arrays = nullptr;
    propagate_gray_captures(result.graph);
    resolve_resets(result.graph);
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

// A net, port or register whose one data input is gray-coded carries the
// same gray code: `assign w = g;`, a port connection, `g_r <= g;`. Run after
// the walk - continuous assigns are visited after the procedural blocks.
void SlangAdapter::propagate_gray_captures(ir::Graph& graph) {
    for (bool changed = true; changed;) {
        changed = false;
        for (auto& node : graph.nodes_mutable()) {
            if (node.is_gray_coded ||
                (node.kind != ir::NodeKind::Net && node.kind != ir::NodeKind::Port &&
                 node.kind != ir::NodeKind::Register))
                continue;
            const ir::Node* input = nullptr;
            bool one_input = true;
            for (uint64_t pred_id : graph.predecessors(node.id)) {
                if (pred_id == node.id)
                    continue;  // a register holding its value
                if (input && input->id != pred_id) {
                    one_input = false;
                    break;
                }
                input = graph.find_node(pred_id);
            }
            if (!one_input || !input)
                continue;
            if (input->is_gray_coded || input->logic_type == ir::LogicType::GrayEncoder) {
                node.is_gray_coded = true;
                changed = true;
            }
        }
    }
}

// The source of each register's reset: back from the reset as its module
// sees it through nets and ports with one driver each, to a top-level port
// (named by its port name, as before), a register (a reset synchronizer),
// logic (a combined reset) or an undriven port, named by their path.
void SlangAdapter::resolve_resets(ir::Graph& graph) {
    for (const auto& [reg_id, path] : reset_paths_) {
        const ir::Node* n = graph.find_node_by_name(path);
        if (!n)
            continue;
        for (int hop = 0; hop < 64; ++hop) {
            if (n->kind != ir::NodeKind::Net && n->kind != ir::NodeKind::Port)
                break;
            if (n->kind == ir::NodeKind::Port &&
                std::count(n->hier_name.begin(), n->hier_name.end(), '.') == 1)
                break;
            auto preds = graph.predecessors(n->id);
            if (preds.size() != 1)
                break;
            const ir::Node* pred = graph.find_node(preds.front());
            if (!pred)
                break;
            n = pred;
        }
        std::string source = n->hier_name;
        if (n->kind == ir::NodeKind::Port && std::count(source.begin(), source.end(), '.') == 1)
            source = source.substr(source.rfind('.') + 1);
        if (auto* reg = graph.find_node_mutable(reg_id))
            reg->reset_signal = source;
    }
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
    scope_paths_[&scope] = p;
    for (auto& member : scope.members()) {
        switch (member.kind) {
            case SymbolKind::Port: {
                auto& port = static_cast<const PortSymbol&>(member);
                std::string port_name = p + "." + sv_to_string(port.name);
                if (!graph.find_node_by_name(port_name)) {
                    auto opt_loc = to_source_loc(port.location);
                    ir::SourceLoc ir_loc = opt_loc.value_or(ir::SourceLoc{});
                    uint32_t port_width = type_width(port.getType());
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

                    // An output connection arrives as the Assignment
                    // `parent = <port>`: its lvalue is the parent signal
                    // (operand collection of an Assignment finds nothing,
                    // which cut every signal leaving an instance).
                    std::vector<std::pair<std::string, uint32_t>> parent_values;
                    collect_actual_values(*expression, p, parent_values);
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
                // Entries of a loop have no name of their own: `g[0]`, `g[1]`, ...
                // (one path for all of them merged the copies into one node).
                for (const auto* entry : array.entries) {
                    if (entry && !entry->isUninstantiated) {
                        std::string entry_prefix =
                            entry->arrayIndex
                                ? array_prefix + "[" +
                                      entry->arrayIndex->toString(LiteralBase::Decimal, false) + "]"
                                : array_prefix + "." + sv_to_string(entry->name);
                        walk_scope(*entry, graph, entry_prefix, module_type);
                    }
                }
                break;
            }
            case SymbolKind::ProceduralBlock: {
                auto& proc = static_cast<const ProceduralBlockSymbol&>(member);
                // A concurrent assertion: no logic, nothing to model.
                if (proc.isFromAssertion)
                    break;
                if (proc.procedureKind == ProceduralBlockKind::AlwaysFF ||
                    proc.procedureKind == ProceduralBlockKind::Always ||
                    proc.procedureKind == ProceduralBlockKind::AlwaysLatch ||
                    proc.procedureKind == ProceduralBlockKind::AlwaysComb) {
                    ClockResetInfo cr;
                    if (proc.procedureKind == ProceduralBlockKind::AlwaysFF ||
                        (proc.procedureKind == ProceduralBlockKind::Always &&
                         !is_level_sensitive(proc)) ||
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
                        if (cr.clock_symbol) {
                            cr.clock = symbol_path(*cr.clock_symbol, p);
                        } else if (!p.empty() && cr.clock.find('.') == std::string::npos &&
                                   cr.clock != "unknown") {
                            cr.clock = p + "." + cr.clock;
                        }
                    } else {
                        cr.clock = "comb";
                    }

                    auto opt_loc = to_source_loc(proc.location);
                    ir::SourceLoc ir_loc = opt_loc.value_or(ir::SourceLoc{});

                    std::vector<AssignInfo> assigns;
                    g_async_reset_symbol = cr.is_async_reset ? cr.reset_symbol : nullptr;
                    collect_assigns(proc.getBody(), p, assigns);
                    g_async_reset_symbol = nullptr;

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
                                if (cr.reset_symbol)
                                    reset_paths_[dst_id] = symbol_path(*cr.reset_symbol, p);
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
                                if (cr.reset_symbol)
                                    reset_paths_[dst_id] = symbol_path(*cr.reset_symbol, p);
                                mutable_dst->is_gray_coded = a.is_gray_transform;
                                if (mutable_dst->module_type.empty())
                                    mutable_dst->module_type = module_type;
                                if (allow_user_annotation_)
                                    apply_pattern_name_tags(*mutable_dst);
                            }
                        }
                        // Tag packed shift chains (sticky max): when one
                        // assignment verifies a pure intra-vector chain, the
                        // multi-bit node models that many pipeline stages.
                        if (a.packed_depth > 0 && cr.clock != "comb") {
                            auto* mutable_dst = graph.find_node_mutable(dst_id);
                            if (mutable_dst && a.packed_depth > mutable_dst->packed_shift_depth)
                                mutable_dst->packed_shift_depth = a.packed_depth;
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
                                graph.add_edge(
                                    src_id, dst_id,
                                    a.is_control ? ir::EdgeRole::Control : ir::EdgeRole::Data);
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
        if (asgn.left().kind == ExpressionKind::Concatenation) {
            // `assign {c, s} = ...`: each element depends on its own
            // right-hand side (see split_concat_assignment).
            for (const auto& t : split_concat_assignment(asgn.left(), asgn.right(), p)) {
                const ir::Node* existing = graph.find_node_by_name(t.lvalue.first);
                uint64_t lhs_id =
                    existing ? existing->id
                             : graph.add_net(t.lvalue.first, t.lvalue.second, {}, p, module_type);
                std::vector<std::pair<std::string, uint32_t>> ops;
                collect_named_values(*t.rhs, p, ops);
                for (const auto& [r, r_width] : ops) {
                    const ir::Node* src = graph.find_node_by_name(r);
                    uint64_t src_id = src ? src->id : graph.add_net(r, r_width, {}, p, module_type);
                    if (src_id && lhs_id)
                        graph.add_edge(src_id, lhs_id);
                }
            }
            continue;
        }
        auto lhs = extract_lvalue(asgn.left(), p);
        if (!lhs)
            continue;
        std::string lhs_name = lhs->first;

        std::vector<std::pair<std::string, uint32_t>> rhs_ops;
        collect_named_values(asgn.right(), p, rhs_ops);
        // A mux: its select operands come first (collect_named_values visits
        // the conditions before the two values) - the clock resolver needs
        // the values of a clock mux, not its select.
        uint32_t mux_selects = 0;
        if (asgn.right().kind == ExpressionKind::ConditionalOp) {
            std::vector<std::pair<std::string, uint32_t>> sel;
            for (const auto& cond :
                 static_cast<const ConditionalExpression&>(asgn.right()).conditions)
                collect_named_values(*cond.expr, p, sel);
            mux_selects = static_cast<uint32_t>(sel.size());
        }

        // `assign g = b ^ (b >> 1)` or `assign g = b2g(b)` encodes gray, as
        // the same right-hand side does in a procedural block.
        bool is_gray =
            is_gray_xor_structure(asgn.right()) ||
            (asgn.right().kind == ExpressionKind::Call &&
             function_computes_gray(static_cast<const CallExpression&>(asgn.right()), p));
        ir::LogicType logic_type = ir::LogicType::Unknown;
        if (is_gray) {
            logic_type = ir::LogicType::GrayEncoder;
        } else if (asgn.right().kind == ExpressionKind::BinaryOp) {
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
            if (is_gray) {
                if (auto* mutable_lhs = graph.find_node_mutable(lhs_id))
                    mutable_lhs->is_gray_coded = true;
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
                if (logic_type == ir::LogicType::Mux) {
                    if (auto* comb = graph.find_node_mutable(comb_id))
                        comb->mux_select_inputs = mux_selects;
                }
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

        // lhs_name is the path of the scope that declares the wire, which is
        // an enclosing one when the assign sits in a generate block.
        std::string qualified_wire = lhs_name;
        std::string wire_name = lhs_name.compare(0, p.size() + 1, p + ".") == 0
                                    ? lhs_name.substr(p.size() + 1)
                                    : lhs_name.substr(lhs_name.rfind('.') + 1);

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
    resolver.set_muxed_clock_domain(muxed_clock_domain_);
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

        // A black-box clock output (an instance port nothing drives) the
        // resolver found is a root of its own, like a top-level port.
        const ir::Node* root_node =
            node.root_clock.empty() ? nullptr : graph.find_node_by_name(node.root_clock);
        bool black_box_root = root_node && root_node->kind == ir::NodeKind::Port &&
                              graph.predecessors(root_node->id).empty();
        // So is a mux of two clocks (ClockResolver::trace_clock_mux).
        bool mux_root = root_node && it != result.clock_map.end() && it->second.is_muxed &&
                        root_node->hier_name == node.root_clock;
        if (!black_box_root && !mux_root && (node.root_clock.empty() || !is_top(node.root_clock))) {
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
                                                    const ir::Graph& graph, int depth) {
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
            // A divided clock: the source of the dividing register's clock
            // (its root may not be resolved yet, and its raw clock name is
            // no root - it split one clock into two domains).
            if (!cn->root_clock.empty())
                return cn->root_clock;
            if (depth < 8 && !cn->clock_domain.empty() && cn->clock_domain != clock_name)
                return trace_to_top_level_source(cn->clock_domain, graph, depth + 1);
            return cn->clock_domain;
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
