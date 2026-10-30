#ifndef OPENCDC_REPORT_DOMAIN_REPORT_H
#define OPENCDC_REPORT_DOMAIN_REPORT_H

#include <ostream>
#include <string>

#include "clock/domain.h"
#include "ir/graph.h"

namespace opencdc::report {

// Inventory of the design's registers, for review next to the findings:
// which registers each clock domain holds, which registers each reset
// signal reaches. Plain text, one register per line, sorted by name, so
// two runs diff cleanly. A register of width N counts as N flip-flops.
class DomainReporter {
   public:
    // One section per clock domain (by name), then the registers whose
    // clock is unresolved (no domain).
    void report_clocks(const ir::Graph& graph, const clock::DomainResult& domains,
                       const std::string& top_module, std::ostream& os) const;

    // One section per reset signal (by name) with the clock domains it
    // reaches, then the registers without a reset.
    void report_resets(const ir::Graph& graph, const clock::DomainResult& domains,
                       const std::string& top_module, std::ostream& os) const;
};

}  // namespace opencdc::report

#endif  // OPENCDC_REPORT_DOMAIN_REPORT_H
