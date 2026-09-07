#ifndef OPENCDC_CDC_RESET_DOMAIN_H
#define OPENCDC_CDC_RESET_DOMAIN_H

#include <string>
#include <unordered_map>
#include <vector>

#include "cdc/crossing.h"
#include "clock/domain.h"
#include "config/config.h"
#include "ir/graph.h"

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
        const ir::Graph& graph, const std::vector<ResetDomain>& reset_domains,
        const std::vector<clock::ClockDomain>& clock_domains,
        const std::unordered_map<uint64_t, size_t>& register_to_clock_domain,
        const config::ResetPolicyConfig* policy = nullptr);

   private:
    const ResetDomain* find_domain_for_register(
        uint64_t register_id, const std::vector<ResetDomain>& domains,
        const std::unordered_map<uint64_t, size_t>& register_to_domain) const;

    bool has_reset_synchronizer(const ir::Graph& graph, uint64_t dst_id,
                                const std::string& src_reset_signal,
                                const std::string& dst_clock_domain) const;
};

}  // namespace opencdc::cdc

#endif  // OPENCDC_CDC_RESET_DOMAIN_H