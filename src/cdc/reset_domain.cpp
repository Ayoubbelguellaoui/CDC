#include "cdc/reset_domain.h"

#include <algorithm>
#include <unordered_set>

namespace opencdc::cdc {

ResetDomainResult ResetDomainAnalyzer::extract_reset_domains(const ir::Graph& graph) {
    ResetDomainResult result;
    std::unordered_map<std::string, size_t> domain_map;

    for (const auto& node : graph.nodes()) {
        if (node.kind != ir::NodeKind::Register)
            continue;
        if (node.reset_signal.empty())
            continue;

        std::string domain_key =
            node.reset_signal + "_" + std::to_string(static_cast<int>(node.reset_pol));

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

bool ResetDomainAnalyzer::has_reset_synchronizer(const ir::Graph& graph, uint64_t dst_id,
                                                 const std::string& src_reset_signal,
                                                 const std::string& dst_clock_domain) const {
    // A reset synchronizer is a 2FF+ chain in the destination clock domain
    // where each stage uses the destination reset signal (not the source reset).
    // The destination register must be downstream of this chain.
    const ir::Node* dst = graph.find_node(dst_id);
    if (!dst || dst->clock_domain != dst_clock_domain)
        return false;

    // Walk backwards from dst through same-domain register predecessors,
    // looking for a 2FF-like chain (2+ registers in the same domain).
    std::vector<uint64_t> visited;
    std::vector<uint64_t> stack = {dst_id};
    size_t chain_length = 0;
    std::string dst_reset = dst->reset_signal;

    while (!stack.empty() && chain_length < 10) {
        uint64_t cur_id = stack.back();
        stack.pop_back();

        if (std::find(visited.begin(), visited.end(), cur_id) != visited.end())
            continue;
        visited.push_back(cur_id);

        const ir::Node* cur = graph.find_node(cur_id);
        if (!cur || cur->kind != ir::NodeKind::Register)
            continue;
        if (cur->clock_domain != dst_clock_domain)
            continue;
        if (cur->width != 1)
            continue;
        if (cur->reset_signal != dst_reset)
            continue;

        chain_length++;

        for (uint64_t pred_id : graph.register_predecessors(cur_id, false)) {
            const ir::Node* pred = graph.find_node(pred_id);
            if (pred && pred->kind == ir::NodeKind::Register &&
                pred->clock_domain == dst_clock_domain && pred->width == 1 &&
                pred->reset_signal == dst_reset) {
                stack.push_back(pred_id);
            }
        }
    }

    // Need at least 2 stages (a proper synchronizer) for reset synchronization.
    return chain_length >= 2;
}

std::vector<Finding> ResetDomainAnalyzer::check_reset_crossings(
    const ir::Graph& graph, const std::vector<ResetDomain>&,
    const std::vector<clock::ClockDomain>& clock_domains,
    const std::unordered_map<uint64_t, size_t>& register_to_clock_domain,
    const config::ResetPolicyConfig* policy) {
    std::vector<Finding> findings;

    auto reset_result = extract_reset_domains(graph);
    const auto& domains = reset_result.domains;

    bool check_same_clock = policy && policy->check_same_clock_reset_crossings;
    bool detect_sync = !policy || policy->detect_reset_synchronizer;

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

            if (src_clock->id == dst_clock->id && !check_same_clock)
                continue;

            // Check for reset synchronizer: if a 2FF+ chain exists in the
            // destination domain using the destination reset signal, the
            // crossing is intentional and safe.
            if (detect_sync &&
                has_reset_synchronizer(graph, dst_id, src.reset_signal, dst_clock->name)) {
                continue;
            }

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

            bool same_clock = src_clock->id == dst_clock->id;
            bool async_crossing = src.is_async_reset || dst->is_async_reset;
            if (async_crossing) {
                f.severity = "error";
                f.reason = "Async reset domain crossing: register '" + src.hier_name +
                           "' (async reset '" + src.reset_signal + "') feeds '" + dst->hier_name +
                           "' (reset '" + dst->reset_signal +
                           "') across clock domains without reset synchronization.";
                f.safety_status = SafetyStatus::VerifiedUnsafe;
                f.safety_provenance = "Async reset domain crossing without reset synchronization";
            } else if (same_clock) {
                f.reason = "Same-clock reset domain crossing: register '" + src.hier_name +
                           "' (reset '" + src.reset_signal + "') feeds '" + dst->hier_name +
                           "' (reset '" + dst->reset_signal +
                           "') in the same clock domain with different reset domains.";
                f.safety_status = SafetyStatus::Candidate;
                f.safety_provenance = "Same-clock reset domain crossing";
            } else {
                f.reason = "Reset domain crossing detected: register '" + src.hier_name +
                           "' uses reset '" + src.reset_signal + "' while register '" +
                           dst->hier_name + "' uses reset '" + dst->reset_signal +
                           "'. Different reset domains may cause metastability issues.";
                f.safety_status = SafetyStatus::VerifiedUnsafe;
                f.safety_provenance = "Reset domain crossing between different reset domains";
            }

            findings.push_back(std::move(f));
        }
    }

    return findings;
}

}  // namespace opencdc::cdc
