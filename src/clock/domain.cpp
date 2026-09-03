#include "clock/domain.h"

#include <unordered_map>

#include "ir/graph.h"

namespace opencdc::clock {

DomainResult DomainExtractor::extract(const ir::Graph& graph) {
    DomainResult result;
    std::unordered_map<std::string, size_t> domain_map;

    for (const auto& node : graph.nodes()) {
        if (node.kind != ir::NodeKind::Register)
            continue;

        std::string domain_name;
        if (!node.root_clock.empty()) {
            domain_name = node.root_clock;
        } else {
            domain_name = node.clock_domain;
        }

        if (domain_name == "unknown" || domain_name.empty()) {
            result.warnings.push_back(
                "Clock relationship unresolved for register '" + node.hier_name +
                "': analysis requires user annotation or additional clock information.");
            continue;
        }

        auto it = domain_map.find(domain_name);
        if (it == domain_map.end()) {
            size_t idx = result.domains.size();
            ClockDomain dom;
            dom.id = next_domain_id_++;
            dom.name = domain_name;
            dom.register_ids.push_back(node.id);
            result.domains.push_back(std::move(dom));
            domain_map[domain_name] = idx;
            result.register_to_domain[node.id] = idx;
        } else {
            result.domains[it->second].register_ids.push_back(node.id);
            result.register_to_domain[node.id] = it->second;
        }
    }

    return result;
}

const ClockDomain* DomainExtractor::find_domain(uint64_t register_id,
                                                const std::vector<ClockDomain>& domains) const {
    for (const auto& dom : domains) {
        for (uint64_t rid : dom.register_ids) {
            if (rid == register_id)
                return &dom;
        }
    }
    return nullptr;
}

bool DomainExtractor::same_domain(uint64_t reg_a, uint64_t reg_b,
                                  const std::vector<ClockDomain>& domains) const {
    const ClockDomain* dom_a = find_domain(reg_a, domains);
    const ClockDomain* dom_b = find_domain(reg_b, domains);
    if (!dom_a || !dom_b)
        return false;
    return dom_a->id == dom_b->id;
}

bool DomainExtractor::same_domain(
    uint64_t reg_a, uint64_t reg_b, const std::vector<ClockDomain>& domains,
    const std::unordered_map<uint64_t, size_t>& register_to_domain) const {
    auto it_a = register_to_domain.find(reg_a);
    auto it_b = register_to_domain.find(reg_b);
    if (it_a == register_to_domain.end() || it_b == register_to_domain.end())
        return false;
    if (it_a->second >= domains.size() || it_b->second >= domains.size())
        return false;
    return domains[it_a->second].id == domains[it_b->second].id;
}

}  // namespace opencdc::clock
