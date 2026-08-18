#include "clock/resolve.h"
#include <unordered_set>

namespace opencdc::clock {

bool ClockResolver::is_clock_port(const std::string& name,
                                  const ir::Graph& graph) const {
    for (const auto& node : graph.nodes()) {
        if (node.kind == ir::NodeKind::Port) {
            size_t dot = node.hier_name.rfind('.');
            std::string short_n = (dot != std::string::npos)
                ? node.hier_name.substr(dot + 1) : node.hier_name;
            if (short_n == name) return true;
        }
    }
    return false;
}

ClockInfo ClockResolver::trace_clock(const std::string& clock_name,
                                     const ir::Graph& graph) const {
    ClockInfo info;
    info.name = clock_name;

    if (is_clock_port(clock_name, graph)) {
        info.root_clock = clock_name;
        return info;
    }

    std::unordered_set<std::string> known_clocks;
    for (const auto& node : graph.nodes()) {
        if (node.kind == ir::NodeKind::Port) {
            size_t dot = node.hier_name.rfind('.');
            std::string short_n = (dot != std::string::npos)
                ? node.hier_name.substr(dot + 1) : node.hier_name;
            known_clocks.insert(short_n);
        }
    }

    for (const auto& known : known_clocks) {
        if (clock_name.size() > known.size() &&
            clock_name.substr(0, known.size()) == known) {
            info.root_clock = known;
            info.is_gated = true;
            return info;
        }
    }

    for (const auto& node : graph.nodes()) {
        if (node.kind == ir::NodeKind::Register &&
            node.clock_domain == clock_name &&
            !node.root_clock.empty()) {
            info.root_clock = node.root_clock;
            info.is_gated = node.clock_is_gated;
            info.is_muxed = node.clock_is_muxed;
            return info;
        }
    }

    info.root_clock = clock_name;
    return info;
}

ResolveResult ClockResolver::resolve(const ir::Graph& graph) {
    ResolveResult result;

    std::unordered_set<std::string> seen_clocks;
    for (const auto& node : graph.nodes()) {
        if (node.kind != ir::NodeKind::Register) continue;
        if (node.clock_domain.empty() || node.clock_domain == "unknown") continue;

        if (seen_clocks.count(node.clock_domain)) continue;
        seen_clocks.insert(node.clock_domain);

        ClockInfo info = trace_clock(node.clock_domain, graph);
        result.clock_map[node.clock_domain] = std::move(info);
    }

    for (const auto& node : graph.nodes()) {
        if (node.kind != ir::NodeKind::Register) continue;
        if (node.clock_domain.empty() || node.clock_domain == "unknown") continue;

        auto it = result.clock_map.find(node.clock_domain);
        if (it == result.clock_map.end()) continue;

        const ClockInfo& ci = it->second;
        if (ci.is_muxed) {
            result.warnings.push_back(
                "Clock '" + node.clock_domain + "' is derived from multiple sources: "
                "analysis requires user annotation.");
        }
    }

    return result;
}

} // namespace opencdc::clock
