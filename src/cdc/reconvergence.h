#ifndef OPENCDC_CDC_RECONVERGENCE_H
#define OPENCDC_CDC_RECONVERGENCE_H

#include "ir/graph.h"
#include "clock/domain.h"
#include "cdc/crossing.h"
#include <vector>

namespace opencdc::cdc {

class ReconvergenceAnalyzer {
public:
    std::vector<Finding> analyze(
        const ir::Graph& graph,
        const std::vector<clock::ClockDomain>& domains,
        const std::vector<Finding>& crossings);

private:
    std::vector<uint64_t> find_fanout_sources(
        const ir::Graph& graph,
        const std::vector<clock::ClockDomain>& domains) const;

    std::vector<Finding> check_pairs(
        const ir::Graph& graph,
        const std::vector<clock::ClockDomain>& domains,
        const std::vector<Finding>& crossings,
        uint64_t source_id) const;
};

} // namespace opencdc::cdc

#endif // OPENCDC_CDC_RECONVERGENCE_H
