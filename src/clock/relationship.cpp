#include "clock/relationship.h"

#include <cmath>

namespace opencdc::clock {

const char* clock_relationship_name(ClockRelationship r) {
    switch (r) {
        case ClockRelationship::Same:
            return "same";
        case ClockRelationship::Synchronous:
            return "synchronous";
        case ClockRelationship::Asynchronous:
            return "asynchronous";
        case ClockRelationship::Generated:
            return "generated";
        case ClockRelationship::Related:
            return "related";
        case ClockRelationship::Exclusive:
            return "exclusive";
        case ClockRelationship::Gated:
            return "gated";
        case ClockRelationship::Muxed:
            return "muxed";
        case ClockRelationship::Unknown:
        default:
            return "unknown";
    }
}

static bool is_exclusive_pair(const ClockConstraints& constraints, const std::string& clk_a,
                              const std::string& clk_b) {
    for (const auto& g : constraints.clock_groups) {
        if (!g.exclusive)
            continue;
        bool a_in = false, b_in = false;
        for (const auto& clk : g.clocks) {
            if (pattern_matches(clk, clk_a))
                a_in = true;
            if (pattern_matches(clk, clk_b))
                b_in = true;
        }
        if (a_in && b_in)
            return true;
    }
    return false;
}

static bool shares_master_clock(const ClockConstraints& constraints, const std::string& clk_a,
                                const std::string& clk_b) {
    auto def_a = constraints.get_clock(clk_a);
    auto def_b = constraints.get_clock(clk_b);
    if (!def_a || !def_b)
        return false;
    if (def_a->is_generated && def_b->is_generated && !def_a->master_clock.empty() &&
        !def_b->master_clock.empty() && def_a->master_clock == def_b->master_clock) {
        return true;
    }
    return false;
}

ClockRelationship classify_relationship(const std::string& clk_a, const std::string& clk_b,
                                        const ClockConstraints& constraints,
                                        const ResolveResult& resolver) {
    if (clk_a == clk_b)
        return ClockRelationship::Same;

    if (constraints.is_asynchronous(clk_a, clk_b))
        return ClockRelationship::Asynchronous;

    if (is_exclusive_pair(constraints, clk_a, clk_b))
        return ClockRelationship::Exclusive;

    auto def_a = constraints.get_clock(clk_a);
    auto def_b = constraints.get_clock(clk_b);

    if (def_a && def_b) {
        if (def_a->is_generated && !def_a->master_clock.empty() &&
            pattern_matches(def_a->master_clock, clk_b)) {
            return ClockRelationship::Generated;
        }
        if (def_b->is_generated && !def_b->master_clock.empty() &&
            pattern_matches(def_b->master_clock, clk_a)) {
            return ClockRelationship::Generated;
        }
    }

    if (shares_master_clock(constraints, clk_a, clk_b))
        return ClockRelationship::Related;

    auto it_a = resolver.clock_map.find(clk_a);
    auto it_b = resolver.clock_map.find(clk_b);
    if (it_a != resolver.clock_map.end() && it_b != resolver.clock_map.end()) {
        if (it_a->second.is_gated && it_b->second.is_gated &&
            it_a->second.root_clock == it_b->second.root_clock) {
            return ClockRelationship::Related;
        }
        if (it_a->second.is_gated && pattern_matches(it_a->second.root_clock, clk_b))
            return ClockRelationship::Gated;
        if (it_b->second.is_gated && pattern_matches(it_b->second.root_clock, clk_a))
            return ClockRelationship::Gated;
        if (it_a->second.is_muxed || it_b->second.is_muxed)
            return ClockRelationship::Muxed;
    }

    if (def_a && def_b && def_a->frequency_mhz > 0 && def_b->frequency_mhz > 0) {
        double ratio = def_a->frequency_mhz / def_b->frequency_mhz;
        double rounded = std::round(ratio);
        if (std::abs(ratio - rounded) < 0.001 && rounded >= 1) {
            return ClockRelationship::Synchronous;
        }
    }

    return ClockRelationship::Unknown;
}

}  // namespace opencdc::clock
