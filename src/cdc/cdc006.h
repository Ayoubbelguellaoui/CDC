#ifndef OPENCDC_CDC_CDC006_H
#define OPENCDC_CDC_CDC006_H

#include <unordered_map>
#include <vector>

#include "cdc/crossing.h"
#include "cdc/synchronizer.h"
#include "clock/domain.h"
#include "ir/graph.h"

namespace opencdc::cdc {

class Cdc006Analyzer {
   public:
    std::vector<Finding> analyze(const ir::Graph& graph,
                                 const std::vector<clock::ClockDomain>& domains,
                                 const std::unordered_map<uint64_t, size_t>& register_to_domain);

   private:
    SynchronizerMatcher sync_matcher_;
};

}  // namespace opencdc::cdc

#endif  // OPENCDC_CDC_CDC006_H
