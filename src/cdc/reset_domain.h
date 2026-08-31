#ifndef OPENCDC_CDC_RESET_DOMAIN_H
#define OPENCDC_CDC_RESET_DOMAIN_H

#include "ir/graph.h"
#include "clock/domain.h"
#include "cdc/crossing.h"
#include <vector>
#include <unordered_map>
#include <string>

namespace opencdc::cdc {

struct ResetDomain {
    std::string name;
    ir::ResetPolarity polarity;
    std::vector<uint64_t> register_ids;
};

struct ResetDomainResult {
    std::vector<ResetDomain> domains;
    std::unordered_map<uint64_t, size_t> register_to_domain;
    std::vector<std::string> warnings;
};

class ResetDomainAnalyzer {
public:
    ResetDomainResult extract_reset_domains(const ir::Graph& graph);
    
    std::vector<Finding> check_reset_crossings(
        const ir::Graph& graph,
        const std::vector<ResetDomain>& reset_domains,
        const std::vector<clock::ClockDomain>& clock_domains,
        const std::unordered_map<uint64_t, size_t>& register_to_clock_domain);
    
private:
    const ResetDomain* find_domain_for_register(
        uint64_t register_id,
        const std::vector<ResetDomain>& domains,
        const std::unordered_map<uint64_t, size_t>& register_to_domain) const;
    
    bool is_async_reset_crossing(
        const ir::Node& src,
        const ir::Node& dst,
        const ResetDomain& src_reset_domain,
        const ResetDomain& dst_reset_domain) const;
};

} // namespace opencdc::cdc

#endif // OPENCDC_CDC_RESET_DOMAIN_H