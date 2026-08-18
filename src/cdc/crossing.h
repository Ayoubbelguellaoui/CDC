#ifndef OPENCDC_CDC_CROSSING_H
#define OPENCDC_CDC_CROSSING_H

#include "ir/graph.h"
#include "clock/domain.h"
#include "cdc/synchronizer.h"
#include <string>
#include <vector>

namespace opencdc::cdc {

struct CrossingPath {
    std::vector<uint64_t> node_ids;
};

struct ReconvergenceInfo {
    bool is_reconvergent = false;
    bool is_hazardous = false;
    uint64_t common_source_id = 0;
    std::string common_source_name;
    std::string explanation;
};

struct Finding {
    std::string rule_id;
    std::string rule_name;
    std::string severity;
    uint64_t source_reg_id = 0;
    uint64_t dest_reg_id = 0;
    std::string source_reg_name;
    std::string dest_reg_name;
    std::string source_domain;
    std::string dest_domain;
    CrossingPath path;
    std::string reason;
    ir::SourceLoc source_loc;
    SyncPattern detected_sync = SyncPattern::None;
    ReconvergenceInfo reconvergence;
    bool waived = false;
    std::string waiver_justification;
    std::string waiver_owner;
};

class CrossingAnalyzer {
public:
    std::vector<Finding> analyze(
        const ir::Graph& graph,
        const std::vector<clock::ClockDomain>& domains);

private:
    const clock::ClockDomain* find_domain_for_node(
        uint64_t node_id,
        const std::vector<clock::ClockDomain>& domains) const;

    std::string build_reason(const Finding& f) const;

    SynchronizerMatcher sync_matcher_;
};

} // namespace opencdc::cdc

#endif // OPENCDC_CDC_CROSSING_H
