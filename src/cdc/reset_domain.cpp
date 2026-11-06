#include "cdc/reset_domain.h"

#include <algorithm>
#include <map>
#include <unordered_set>

#include "cdc/synchronizer.h"
#include "config/config.h"

namespace opencdc::cdc {

ResetDomainResult ResetDomainAnalyzer::extract_reset_domains(const ir::Graph& graph) {
    ResetDomainResult result;
    std::unordered_map<std::string, size_t> domain_map;

    for (const auto& node : graph.nodes()) {
        if (node.kind != ir::NodeKind::Register)
            continue;
        if (node.reset_signal.empty())
            continue;

        std::string domain_key = node.reset_signal + "_" +
                                 std::to_string(static_cast<int>(node.reset_pol)) + "_" +
                                 (node.is_async_reset ? "async" : "sync");

        auto it = domain_map.find(domain_key);
        if (it == domain_map.end()) {
            size_t idx = result.domains.size();
            ResetDomain domain;
            domain.name = node.reset_signal;
            domain.polarity = node.reset_pol;
            domain.register_ids.push_back(node.id);
            result.domains.push_back(std::move(domain));
            domain_map[domain_key] = idx;
            result.register_to_domain[node.id] = idx;
        } else {
            result.domains[it->second].register_ids.push_back(node.id);
            result.register_to_domain[node.id] = it->second;
        }
    }

    return result;
}

const ResetDomain* ResetDomainAnalyzer::find_domain_for_register(
    uint64_t register_id, const std::vector<ResetDomain>& domains,
    const std::unordered_map<uint64_t, size_t>& register_to_domain) const {
    auto it = register_to_domain.find(register_id);
    if (it == register_to_domain.end())
        return nullptr;
    if (it->second >= domains.size())
        return nullptr;
    return &domains[it->second];
}

std::vector<Finding> ResetDomainAnalyzer::check_reset_crossings(
    const ir::Graph& graph, const std::vector<ResetDomain>&,
    const std::vector<clock::ClockDomain>& clock_domains,
    const std::unordered_map<uint64_t, size_t>& register_to_clock_domain,
    const config::ResetPolicyConfig* policy) {
    std::vector<Finding> findings;

    auto reset_result = extract_reset_domains(graph);
    const auto& domains = reset_result.domains;

    for (const auto& src : graph.nodes()) {
        if (src.kind != ir::NodeKind::Register)
            continue;
        if (src.reset_signal.empty())
            continue;

        auto src_it = reset_result.register_to_domain.find(src.id);
        if (src_it == reset_result.register_to_domain.end())
            continue;
        if (src_it->second >= domains.size())
            continue;
        const ResetDomain* src_reset_domain = &domains[src_it->second];

        for (uint64_t dst_id : graph.register_successors(src.id)) {
            const ir::Node* dst = graph.find_node(dst_id);
            if (!dst || dst->kind != ir::NodeKind::Register)
                continue;
            if (dst->reset_signal.empty())
                continue;

            auto dst_it = reset_result.register_to_domain.find(dst_id);
            if (dst_it == reset_result.register_to_domain.end())
                continue;
            if (dst_it->second >= domains.size())
                continue;
            const ResetDomain* dst_reset_domain = &domains[dst_it->second];

            if (src_reset_domain->name == dst_reset_domain->name &&
                src_reset_domain->polarity == dst_reset_domain->polarity)
                continue;

            auto clock_it = register_to_clock_domain.find(src.id);
            if (clock_it == register_to_clock_domain.end())
                continue;
            if (clock_it->second >= clock_domains.size())
                continue;
            const clock::ClockDomain* src_clock = &clock_domains[clock_it->second];

            clock_it = register_to_clock_domain.find(dst_id);
            if (clock_it == register_to_clock_domain.end())
                continue;
            if (clock_it->second >= clock_domains.size())
                continue;
            const clock::ClockDomain* dst_clock = &clock_domains[clock_it->second];

            if (src_clock->id == dst_clock->id) {
                bool check_same = policy && policy->check_same_clock_reset_crossings;
                if (!check_same)
                    continue;
            }

            // Across clocks into a synchronizer: the source's asynchronous
            // reset is one more asynchronous change of the source, which the
            // destination's chain resolves like any other (policy).
            if (policy && policy->synchronized_crossings_safe && src_clock->id != dst_clock->id &&
                SynchronizerMatcher().find_pattern_for_dest(dst_id, graph, true) !=
                    SyncPattern::None)
                continue;

            // Reset synchronizer suppression: dst with 2-stage same-reset chain.
            if (policy && policy->detect_reset_synchronizer &&
                has_reset_synchronizer(graph, dst_id, dst->reset_signal))
                continue;

            Finding f;
            f.rule_id = "CDC009";
            f.rule_name = "reset_domain_crossing";
            f.severity = "warning";
            f.source_reg_id = src.id;
            f.dest_reg_id = dst_id;
            f.source_reg_name = src.hier_name;
            f.dest_reg_name = dst->hier_name;
            f.source_domain = src_clock->name;
            f.dest_domain = dst_clock->name;
            f.path.node_ids = {src.id, dst_id};
            f.source_loc = src.loc;
            f.bus_width = src.width;

            bool async_crossing = src.is_async_reset || dst->is_async_reset;
            if (async_crossing) {
                f.severity = "error";
                f.safety_status = SafetyStatus::VerifiedUnsafe;
                f.safety_provenance = "Async reset domain crossing without synchronization";
                f.reason = "Async reset domain crossing: register '" + src.hier_name +
                           "' (async reset '" + src.reset_signal + "') feeds '" + dst->hier_name +
                           "' (reset '" + dst->reset_signal +
                           "') across clock domains without reset synchronization.";
            } else {
                f.safety_status = SafetyStatus::VerifiedUnsafe;
                f.safety_provenance = "Reset domain crossing without synchronization";
                f.reason = "Reset domain crossing detected: register '" + src.hier_name +
                           "' uses reset '" + src.reset_signal + "' while register '" +
                           dst->hier_name + "' uses reset '" + dst->reset_signal +
                           "'. Different reset domains may cause metastability issues.";
            }

            findings.push_back(std::move(f));
        }
    }

    // A reset synchronized in one clock domain and used as the asynchronous
    // reset of another clock's registers: its release reaches them
    // asynchronously to their clock - the synchronizer is in the wrong
    // domain. The source is the register the reset traces to
    // (SlangAdapter::resolve_resets).
    auto clock_of = [&](uint64_t id) -> const clock::ClockDomain* {
        auto it = register_to_clock_domain.find(id);
        if (it == register_to_clock_domain.end() || it->second >= clock_domains.size())
            return nullptr;
        return &clock_domains[it->second];
    };
    for (const auto& reg : graph.nodes()) {
        if (reg.kind != ir::NodeKind::Register || !reg.is_async_reset || reg.reset_signal.empty())
            continue;
        const ir::Node* source = graph.find_node_by_name(reg.reset_signal);
        if (!source || source->kind != ir::NodeKind::Register || source->id == reg.id)
            continue;
        const clock::ClockDomain* reg_clock = clock_of(reg.id);
        const clock::ClockDomain* source_clock = clock_of(source->id);
        if (!reg_clock || !source_clock || reg_clock->id == source_clock->id)
            continue;
        Finding f;
        f.rule_id = "CDC009";
        f.rule_name = "reset_domain_crossing";
        f.severity = "error";
        f.source_reg_id = source->id;
        f.dest_reg_id = reg.id;
        f.source_reg_name = source->hier_name;
        f.dest_reg_name = reg.hier_name;
        f.source_domain = source_clock->name;
        f.dest_domain = reg_clock->name;
        f.path.node_ids = {source->id, reg.id};
        f.source_loc = source->loc;
        f.bus_width = reg.width;
        f.safety_status = SafetyStatus::VerifiedUnsafe;
        f.safety_provenance = "Async reset released from another clock domain";
        f.reason = "The asynchronous reset of '" + reg.hier_name + "' (clock domain '" +
                   reg_clock->name + "') comes from register '" + source->hier_name +
                   "' in clock domain '" + source_clock->name +
                   "': its release is not synchronized to the register's clock.";
        findings.push_back(std::move(f));
    }

    return findings;
}

// The node an asynchronous reset comes from, as SlangAdapter::resolve_resets
// names it (reset_signal), followed on: through nets and inner ports with
// one driver, and through a mux whose select is a port held constant by
// set_case_analysis (a test-mode reset bypass in functional mode). nullptr
// when the reset names a top-level port the graph has no node for.
static const ir::Node* trace_reset(const ir::Graph& graph, const ir::Node& reg,
                                   const std::unordered_map<std::string, int>* constants) {
    auto is_top_port = [](const ir::Node* n) {
        return n->kind == ir::NodeKind::Port &&
               std::count(n->hier_name.begin(), n->hier_name.end(), '.') == 1;
    };
    // The constant of a mux select: back through single-driver nets to a
    // top-level port named in the case analysis; -1 if none.
    auto constant_of = [&](uint64_t id) -> int {
        const ir::Node* n = graph.find_node(id);
        for (int hop = 0; n && hop < 64; ++hop) {
            if (is_top_port(n)) {
                auto it = constants->find(n->hier_name.substr(n->hier_name.find('.') + 1));
                return it == constants->end() ? -1 : it->second;
            }
            auto preds = graph.predecessors(n->id);
            if (n->kind == ir::NodeKind::Register || preds.size() != 1)
                return -1;
            n = graph.find_node(preds.front());
        }
        return -1;
    };
    const ir::Node* n = graph.find_node_by_name(reg.reset_signal);
    for (int hop = 0; n && hop < 64; ++hop) {
        if (is_top_port(n))
            return n;
        if (n->kind == ir::NodeKind::Net || n->kind == ir::NodeKind::Port) {
            auto preds = graph.predecessors(n->id);
            if (preds.size() != 1)
                return n;
            n = graph.find_node(preds.front());
            continue;
        }
        if (constants && !constants->empty() && n->kind == ir::NodeKind::Combinational &&
            n->logic_type == ir::LogicType::Mux && n->mux_select_inputs <= 1 &&
            n->logic_inputs.size() == 3) {
            int sel = constant_of(n->logic_inputs[0]);
            if (sel >= 0) {
                n = graph.find_node(n->logic_inputs[sel ? 1 : 2]);
                continue;
            }
        }
        return n;
    }
    return n;
}

// The name of a reset source for a finding: the port name, or the node
// path without the internal suffix of a logic node ("rst$comb" -> "rst").
static std::string reset_name(const ir::Node* source, const ir::Node& reg) {
    if (!source)
        return reg.reset_signal;
    std::string name = source->hier_name;
    if (source->kind == ir::NodeKind::Port && std::count(name.begin(), name.end(), '.') == 1)
        return name.substr(name.find('.') + 1);
    size_t dollar = name.find('$');
    return dollar == std::string::npos ? name : name.substr(0, dollar);
}

std::vector<Finding> ResetDomainAnalyzer::check_reset_release(
    const ir::Graph& graph, const std::vector<clock::ClockDomain>& clock_domains,
    const std::unordered_map<uint64_t, size_t>& register_to_clock_domain,
    const std::unordered_map<std::string, int>* constants) const {
    auto clock_of = [&](uint64_t id) -> long {
        auto it = register_to_clock_domain.find(id);
        if (it == register_to_clock_domain.end() || it->second >= clock_domains.size())
            return -1;
        return static_cast<long>(it->second);
    };

    // Registers whose output reaches the asynchronous reset of other
    // registers - directly or through logic (a test-mode mux, a reset
    // combined with another): the last stage of a reset synchronizer.
    std::unordered_set<uint64_t> reset_sources;
    std::unordered_set<uint64_t> traced;
    for (const auto& reg : graph.nodes()) {
        if (reg.kind != ir::NodeKind::Register || !reg.is_async_reset || reg.reset_signal.empty())
            continue;
        const ir::Node* start = graph.find_node_by_name(reg.reset_signal);
        if (!start || !traced.insert(start->id).second)
            continue;
        std::vector<uint64_t> todo{start->id};
        std::unordered_set<uint64_t> seen{start->id};
        while (!todo.empty() && seen.size() < 256) {
            const ir::Node* n = graph.find_node(todo.back());
            todo.pop_back();
            if (!n)
                continue;
            if (n->kind == ir::NodeKind::Register) {
                if (n->id != reg.id)
                    reset_sources.insert(n->id);
                continue;
            }
            for (uint64_t p : graph.predecessors(n->id))
                if (seen.insert(p).second)
                    todo.push_back(p);
        }
    }

    // A register released by the raw reset is fine when it is a stage of a
    // reset synchronizer: up to four registers on the same reset and clock,
    // ending on a register used as a reset.
    constexpr int kMaxStages = 4;
    auto in_reset_synchronizer = [&](const ir::Node& reg) {
        long clock = clock_of(reg.id);
        std::vector<uint64_t> stage{reg.id};
        std::unordered_set<uint64_t> seen{reg.id};
        for (int i = 0; i < kMaxStages && !stage.empty(); ++i) {
            std::vector<uint64_t> next;
            for (uint64_t id : stage) {
                if (reset_sources.count(id))
                    return true;
                for (uint64_t succ : graph.register_successors(id)) {
                    const ir::Node* n = graph.find_node(succ);
                    if (n && n->kind == ir::NodeKind::Register &&
                        n->reset_signal == reg.reset_signal && clock_of(succ) == clock &&
                        seen.insert(succ).second)
                        next.push_back(succ);
                }
            }
            stage = std::move(next);
        }
        return false;
    };

    // Registers released by a port or logic, per reset and clock domain. A
    // reset from a register of another clock is CDC009.
    std::map<std::pair<std::string, long>, std::vector<const ir::Node*>> released;
    for (const auto& reg : graph.nodes()) {
        if (reg.kind != ir::NodeKind::Register || !reg.is_async_reset || reg.reset_signal.empty())
            continue;
        const ir::Node* source = trace_reset(graph, reg, constants);
        if (source && source->kind == ir::NodeKind::Register)
            continue;
        long clock = clock_of(reg.id);
        if (clock < 0 || in_reset_synchronizer(reg))
            continue;
        released[{reset_name(source, reg), clock}].push_back(&reg);
    }

    std::vector<Finding> findings;
    for (auto& [key, regs] : released) {
        std::sort(regs.begin(), regs.end(),
                  [](const ir::Node* a, const ir::Node* b) { return a->hier_name < b->hier_name; });
        const ir::Node* first = regs.front();
        const auto& clock = clock_domains[static_cast<size_t>(key.second)];
        Finding f;
        f.rule_id = "CDC014";
        f.rule_name = "unsynchronized_reset_release";
        f.severity = "error";
        f.source_reg_id = first->id;
        f.dest_reg_id = first->id;
        f.source_reg_name = key.first;
        f.dest_reg_name = first->hier_name;
        f.source_domain = key.first;
        f.dest_domain = clock.name;
        f.path.node_ids = {first->id};
        f.source_loc = first->loc;
        f.bus_width = first->width;
        f.safety_status = SafetyStatus::VerifiedUnsafe;
        f.safety_provenance = "Async reset released without a reset synchronizer";
        f.reason = "Asynchronous reset '" + key.first + "' is released into clock domain '" +
                   clock.name +
                   "' without a reset synchronizer of that domain: " + std::to_string(regs.size()) +
                   " register(s), e.g. '" + first->hier_name +
                   "', leave reset asynchronously to their clock.";
        findings.push_back(std::move(f));
    }
    return findings;
}

bool ResetDomainAnalyzer::has_reset_synchronizer(const ir::Graph& graph, uint64_t dst_id,
                                                 const std::string& reset_signal) const {
    // Walk backwards: need 2 predecessor stages with same reset signal.
    auto preds1 = graph.register_predecessors(dst_id, false);
    for (uint64_t p1 : preds1) {
        const ir::Node* n1 = graph.find_node(p1);
        if (!n1 || n1->reset_signal != reset_signal)
            continue;
        auto preds2 = graph.register_predecessors(p1, false);
        for (uint64_t p2 : preds2) {
            const ir::Node* n2 = graph.find_node(p2);
            if (n2 && n2->reset_signal == reset_signal)
                return true;
        }
    }
    return false;
}

}  // namespace opencdc::cdc
