#include "cdc/reset_domain.h"

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

std::vector<Finding> ResetDomainAnalyzer::check_reset_crossings(
    const ir::Graph& graph, const std::vector<ResetDomain>&,
    const std::vector<clock::ClockDomain>& clock_domains,
    const std::unordered_map<uint64_t, size_t>& register_to_clock_domain) {
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

            if (src_clock->id == dst_clock->id)
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
                f.reason = "Async reset domain crossing: register '" + src.hier_name +
                           "' (async reset '" + src.reset_signal + "') feeds '" + dst->hier_name +
                           "' (reset '" + dst->reset_signal +
                           "') across clock domains without reset synchronization.";
            } else {
                f.reason = "Reset domain crossing detected: register '" + src.hier_name +
                           "' uses reset '" + src.reset_signal + "' while register '" +
                           dst->hier_name + "' uses reset '" + dst->reset_signal +
                           "'. Different reset domains may cause metastability issues.";
            }

            findings.push_back(std::move(f));
        }
    }

    return findings;
}

}  // namespace opencdc::cdc
