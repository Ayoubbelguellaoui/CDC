#ifndef OPENCDC_CLOCK_RESOLVE_H
#define OPENCDC_CLOCK_RESOLVE_H

#include "ir/graph.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace opencdc::clock {

struct ClockInfo {
    std::string name;
    std::string root_clock;
    bool is_gated = false;
    bool is_muxed = false;
};

struct ResolveResult {
    std::unordered_map<std::string, ClockInfo> clock_map;
    std::vector<std::string> warnings;
};

class ClockResolver {
public:
    ResolveResult resolve(const ir::Graph& graph);

private:
    bool is_clock_port(const std::string& name,
                       const ir::Graph& graph) const;

    std::string get_root_port_name(const std::string& name,
                                   const ir::Graph& graph) const;

    std::string trace_to_top_port(const std::string& port_name,
                                  const ir::Graph& graph) const;

    ClockInfo trace_clock(const std::string& clock_name,
                          const ir::Graph& graph) const;
};

} // namespace opencdc::clock

#endif // OPENCDC_CLOCK_RESOLVE_H
