#ifndef OPENCDC_CLOCK_RELATIONSHIP_H
#define OPENCDC_CLOCK_RELATIONSHIP_H

#include "clock/constraints.h"
#include "clock/resolve.h"

namespace opencdc::clock {

enum class ClockRelationship {
    Same,
    Synchronous,
    Asynchronous,
    Generated,
    Related,
    Exclusive,
    Gated,
    Muxed,
    Unknown
};

const char* clock_relationship_name(ClockRelationship r);

ClockRelationship classify_relationship(const std::string& clk_a, const std::string& clk_b,
                                        const ClockConstraints& constraints,
                                        const ResolveResult& resolver);

}  // namespace opencdc::clock

#endif  // OPENCDC_CLOCK_RELATIONSHIP_H
