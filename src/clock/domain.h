#ifndef OPENCDC_CLOCK_DOMAIN_H
#define OPENCDC_CLOCK_DOMAIN_H

#include "ir/graph.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace opencdc::clock {

struct ClockDomain {
    uint64_t id;
    std::string name;
    std::vector<uint64_t> register_ids;
};

struct DomainResult {
    std::vector<ClockDomain> domains;
    std::vector<std::string> warnings;
    std::unordered_map<uint64_t, size_t> register_to_domain;
};

class DomainExtractor {
public:
    DomainResult extract(const ir::Graph& graph);

    const ClockDomain* find_domain(uint64_t register_id,
                                   const std::vector<ClockDomain>& domains) const;

    bool same_domain(uint64_t reg_a, uint64_t reg_b,
                     const std::vector<ClockDomain>& domains) const;

private:
    uint64_t next_domain_id_ = 1;
};

} // namespace opencdc::clock

#endif // OPENCDC_CLOCK_DOMAIN_H
