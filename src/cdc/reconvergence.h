#ifndef OPENCDC_CDC_RECONVERGENCE_H
#define OPENCDC_CDC_RECONVERGENCE_H

#include <vector>

#include "cdc/crossing.h"
#include "clock/domain.h"
#include "ir/graph.h"

namespace opencdc::cdc {

class ReconvergenceAnalyzer {
   public:
    std::vector<Finding> analyze(const ir::Graph& graph,
                                 const std::vector<clock::ClockDomain>& domains,
                                 const std::vector<Finding>& crossings);

    void set_max_hops(int hops) {
        max_hops_ = hops < 1 ? 1 : hops;
    }

    // Config::report_double_synchronization.
    void set_report_double_synchronization(bool on) {
        report_double_sync_ = on;
    }

   private:
    int max_hops_ = 3;
    bool report_double_sync_ = false;
    std::vector<uint64_t> find_fanout_sources(const ir::Graph& graph,
                                              const std::vector<clock::ClockDomain>& domains) const;

    std::vector<Finding> check_pairs(const ir::Graph& graph,
                                     const std::vector<clock::ClockDomain>& domains,
                                     const std::vector<Finding>& crossings,
                                     uint64_t source_id) const;

    // Where the value of a destination register goes: the registers within
    // max_hops_, and the top-level output ports reached from them through
    // nets and logic - a reconvergence can surface at a port too.
    std::vector<uint64_t> meeting_points(const ir::Graph& graph, uint64_t start_id) const;

    // Does `from` feed `to` within max_hops_ registers of to's clock (a delayed
    // or derived copy)? Two such sources are correlated.
    bool derives_from(const ir::Graph& graph, uint64_t to, uint64_t from) const;

    // Two correlated sources, synchronized separately, whose copies meet.
    std::vector<Finding> check_correlated(const ir::Graph& graph,
                                          const std::vector<Finding>& crossings) const;
};

}  // namespace opencdc::cdc

#endif  // OPENCDC_CDC_RECONVERGENCE_H
