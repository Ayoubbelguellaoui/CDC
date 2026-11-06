#ifndef OPENCDC_CLOCK_RESOLVE_H
#define OPENCDC_CLOCK_RESOLVE_H

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ir/graph.h"

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

    // Config::muxed_clock_domain (off: a mux is represented by an input).
    void set_muxed_clock_domain(bool on) {
        muxed_clock_domain_ = on;
    }

   private:
    bool is_clock_port(const std::string& name, const ir::Graph& graph) const;

    std::string get_root_port_name(const std::string& name, const ir::Graph& graph) const;

    std::string trace_to_top_port(const std::string& port_name, const ir::Graph& graph) const;

    ClockInfo trace_clock(const std::string& clock_name, const ir::Graph& graph) const;

    // Source of a clock through the hierarchy: walks the drivers of the
    // clock node back through nets, instance ports and gating/muxing logic
    // (not through registers) to the top-level ports that feed it. Empty
    // root_clock when that does not decide the source.
    // require_child_output: decide only clocks that leave an instance through
    // an output port (the others keep the existing tracing); off, any clock
    // with a decidable source (the inputs of a clock mux).
    ClockInfo trace_through_hierarchy(const std::string& clock_name, const ir::Graph& graph,
                                      bool require_child_output = true) const;

    // A clock driven by a mux whose values trace to two or more different
    // roots is a clock domain of its own, named by the clock node, so that
    // its registers cross from both. Empty root_clock otherwise.
    ClockInfo trace_clock_mux(const std::string& clock_name, const ir::Graph& graph,
                              int depth) const;

    bool muxed_clock_domain_ = false;

    mutable std::unordered_set<std::string> port_names_;
    mutable std::unordered_set<std::string> short_port_names_;
    mutable bool port_names_built_ = false;

    mutable std::unordered_map<std::string, const ir::Node*> port_by_name_;
    mutable std::unordered_map<std::string, std::vector<std::string>> short_to_hier_;
    mutable bool port_index_built_ = false;

    void ensure_port_names(const ir::Graph& graph) const;
    void ensure_port_index(const ir::Graph& graph) const;
};

}  // namespace opencdc::clock

#endif  // OPENCDC_CLOCK_RESOLVE_H
