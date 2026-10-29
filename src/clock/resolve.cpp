#include "clock/resolve.h"

#include <algorithm>
#include <unordered_set>

namespace opencdc::clock {

void ClockResolver::ensure_port_names(const ir::Graph& graph) const {
    if (port_names_built_)
        return;
    for (const auto& node : graph.nodes()) {
        if (node.kind == ir::NodeKind::Port) {
            port_names_.insert(node.hier_name);
            size_t dot = node.hier_name.rfind('.');
            if (dot != std::string::npos) {
                short_port_names_.insert(node.hier_name.substr(dot + 1));
            }
        }
    }
    port_names_built_ = true;
}

void ClockResolver::ensure_port_index(const ir::Graph& graph) const {
    if (port_index_built_)
        return;
    for (const auto& node : graph.nodes()) {
        if (node.kind == ir::NodeKind::Port) {
            port_by_name_[node.hier_name] = &node;
            size_t dot = node.hier_name.rfind('.');
            if (dot != std::string::npos) {
                short_to_hier_[node.hier_name.substr(dot + 1)].push_back(node.hier_name);
            }
        }
    }
    port_index_built_ = true;
}

bool ClockResolver::is_clock_port(const std::string& name, const ir::Graph& graph) const {
    ensure_port_names(graph);
    return port_names_.count(name) || short_port_names_.count(name);
}

std::string ClockResolver::get_root_port_name(const std::string& name,
                                              const ir::Graph& graph) const {
    ensure_port_names(graph);
    if (port_names_.count(name))
        return name;
    ensure_port_index(graph);
    size_t dot = name.rfind('.');
    std::string short_name = (dot != std::string::npos) ? name.substr(dot + 1) : name;
    auto it = short_to_hier_.find(short_name);
    if (it != short_to_hier_.end() && !it->second.empty()) {
        // Deterministic tie-break independent of elaboration order: smallest
        // hierarchical name wins when a leaf name maps to several ports.
        return *std::min_element(it->second.begin(), it->second.end());
    }
    return name;
}

std::string ClockResolver::trace_to_top_port(const std::string& port_name,
                                             const ir::Graph& graph) const {
    std::string current = port_name;

    size_t dot = current.rfind('.');
    std::string short_name = (dot != std::string::npos) ? current.substr(dot + 1) : current;

    ensure_port_index(graph);
    auto it = short_to_hier_.find(short_name);
    int has_same_short_count =
        (it != short_to_hier_.end()) ? static_cast<int>(it->second.size()) : 0;
    if (has_same_short_count <= 1)
        return short_name;

    for (int depth = 0; depth < 10; ++depth) {
        bool found_parent = false;
        auto pit = port_by_name_.find(current);
        if (pit != port_by_name_.end()) {
            const ir::Node* node = pit->second;
            for (uint64_t pred_id : graph.predecessors(node->id)) {
                const ir::Node* pred = graph.find_node(pred_id);
                if (!pred)
                    continue;
                if (pred->kind == ir::NodeKind::Port && is_clock_port(pred->hier_name, graph)) {
                    current = pred->hier_name;
                    found_parent = true;
                    break;
                }
                if (pred->kind == ir::NodeKind::Net) {
                    for (uint64_t net_pred_id : graph.predecessors(pred_id)) {
                        const ir::Node* net_pred = graph.find_node(net_pred_id);
                        if (!net_pred)
                            continue;
                        if (net_pred->kind == ir::NodeKind::Port &&
                            is_clock_port(net_pred->hier_name, graph)) {
                            current = net_pred->hier_name;
                            found_parent = true;
                            break;
                        }
                    }
                    if (found_parent)
                        break;
                }
            }
        }
        if (!found_parent)
            break;
    }

    dot = current.rfind('.');
    return (dot != std::string::npos) ? current.substr(dot + 1) : current;
}

ClockInfo ClockResolver::trace_through_hierarchy(const std::string& clock_name,
                                                 const ir::Graph& graph) const {
    ClockInfo info;
    info.name = clock_name;
    const ir::Node* start = graph.find_node_by_name(clock_name);
    if (!start)
        return info;
    auto is_top_port = [](const ir::Node& n) {
        return n.kind == ir::NodeKind::Port &&
               std::count(n.hier_name.begin(), n.hier_name.end(), '.') == 1;
    };
    if (is_top_port(*start))
        return info;  // a top-level clock: the existing tracing names it

    // Every top-level port reachable backwards; a child's output port is
    // driven from inside the child (a clock gate's `clk_o = en & clk_i`),
    // so ports are passed through like nets.
    std::vector<const ir::Node*> roots;
    bool through_child_output = false;
    bool gated = false;
    bool muxed = false;
    std::unordered_set<uint64_t> visited{start->id};
    std::vector<const ir::Node*> stack{start};
    while (!stack.empty() && visited.size() < 4096) {
        const ir::Node* n = stack.back();
        stack.pop_back();
        for (uint64_t pred_id : graph.predecessors(n->id)) {
            const ir::Node* pred = graph.find_node(pred_id);
            if (!pred || !visited.insert(pred_id).second)
                continue;
            if (is_top_port(*pred)) {
                roots.push_back(pred);
                continue;
            }
            if (pred->kind == ir::NodeKind::Register)
                continue;  // a divided clock: the existing tracing folds it
            if (pred->kind == ir::NodeKind::Combinational) {
                gated = gated || pred->logic_type == ir::LogicType::And;
                muxed = muxed || pred->logic_type == ir::LogicType::Mux;
            }
            if (pred->kind == ir::NodeKind::Port && n->kind == ir::NodeKind::Net &&
                pred->module_path.size() > n->module_path.size())
                through_child_output = true;
            stack.push_back(pred);
        }
    }
    // Only a clock that leaves a child instance through an output port is
    // decided here; other shapes keep the existing tracing.
    if (!through_child_output || roots.empty())
        return info;

    // A clock gate or mux reaches its enable or select too: the source is
    // the top-level port that clocks registers directly.
    const ir::Node* root = nullptr;
    if (roots.size() == 1) {
        root = roots.front();
    } else {
        for (const ir::Node* r : roots) {
            bool clocks_registers = false;
            for (const auto& node : graph.nodes()) {
                if (node.kind == ir::NodeKind::Register && node.clock_domain == r->hier_name) {
                    clocks_registers = true;
                    break;
                }
            }
            if (clocks_registers) {
                if (root)
                    return info;  // two clock sources: undecided
                root = r;
            }
        }
    }
    if (!root)
        return info;
    size_t dot = root->hier_name.rfind('.');
    info.root_clock = root->hier_name.substr(dot + 1);
    info.is_gated = gated && !muxed;
    info.is_muxed = muxed;
    return info;
}

ClockInfo ClockResolver::trace_clock(const std::string& clock_name, const ir::Graph& graph) const {
    ClockInfo info;
    info.name = clock_name;

    ensure_port_index(graph);

    ClockInfo through = trace_through_hierarchy(clock_name, graph);
    if (!through.root_clock.empty())
        return through;

    if (is_clock_port(clock_name, graph)) {
        info.root_clock = trace_to_top_port(clock_name, graph);
        // Check if the clock port drives gating/muxing logic (e.g. And/Mux nets).
        auto hit = port_by_name_.find(clock_name);
        if (hit != port_by_name_.end()) {
            const ir::Node* node = hit->second;
            for (uint64_t succ : graph.successors(node->id)) {
                const ir::Node* sn = graph.find_node(succ);
                if (!sn || sn->kind != ir::NodeKind::Net)
                    continue;
                if (sn->logic_type == ir::LogicType::And)
                    info.is_gated = true;
                if (sn->logic_type == ir::LogicType::Mux)
                    info.is_muxed = true;
            }
        }
        return info;
    }

    {
        auto hit = port_by_name_.find(clock_name);
        if (hit != port_by_name_.end()) {
            info.root_clock = trace_to_top_port(clock_name, graph);
            return info;
        }
    }

    for (const auto& node : graph.nodes()) {
        if (node.kind == ir::NodeKind::Net) {
            bool match = (node.hier_name == clock_name);
            if (!match) {
                size_t dot = node.hier_name.rfind('.');
                if (dot != std::string::npos && node.hier_name.substr(dot + 1) == clock_name) {
                    match = true;
                }
            }
            if (!match)
                continue;

            for (uint64_t pred_id : graph.predecessors(node.id)) {
                const ir::Node* pred = graph.find_node(pred_id);
                if (!pred)
                    continue;
                if (pred->kind == ir::NodeKind::Combinational) {
                    const ir::Node* clock_port = nullptr;
                    for (uint64_t cp_id : graph.predecessors(pred_id)) {
                        const ir::Node* cp = graph.find_node(cp_id);
                        if (!cp)
                            continue;
                        if (cp->kind == ir::NodeKind::Port) {
                            if (is_clock_port(cp->hier_name, graph)) {
                                clock_port = cp;
                                if (pred->logic_type == ir::LogicType::And)
                                    break;
                            }
                        }
                    }
                    if (clock_port) {
                        info.root_clock = trace_to_top_port(clock_port->hier_name, graph);
                        info.is_gated = (pred->logic_type == ir::LogicType::And);
                        info.is_muxed = (pred->logic_type == ir::LogicType::Mux);
                        return info;
                    }
                } else if (pred->kind == ir::NodeKind::Port) {
                    if (is_clock_port(pred->hier_name, graph)) {
                        info.root_clock = trace_to_top_port(pred->hier_name, graph);
                        return info;
                    }
                } else if (pred->kind == ir::NodeKind::Net) {
                    // Net→Net chain: follow predecessors with depth limit.
                    std::unordered_set<uint64_t> visited;
                    visited.insert(pred_id);
                    const ir::Node* cur = pred;
                    for (int hop = 0; hop < 8 && cur; ++hop) {
                        bool found = false;
                        for (uint64_t np_id : graph.predecessors(cur->id)) {
                            if (visited.count(np_id))
                                continue;
                            visited.insert(np_id);
                            const ir::Node* np = graph.find_node(np_id);
                            if (!np)
                                continue;
                            if (np->kind == ir::NodeKind::Port &&
                                is_clock_port(np->hier_name, graph)) {
                                info.root_clock = trace_to_top_port(np->hier_name, graph);
                                return info;
                            }
                            if (np->kind == ir::NodeKind::Combinational) {
                                // Check direct Port predecessors.
                                for (uint64_t cp_id : graph.predecessors(np_id)) {
                                    const ir::Node* cp = graph.find_node(cp_id);
                                    if (cp && cp->kind == ir::NodeKind::Port &&
                                        is_clock_port(cp->hier_name, graph)) {
                                        info.root_clock = trace_to_top_port(cp->hier_name, graph);
                                        info.is_gated = (np->logic_type == ir::LogicType::And);
                                        info.is_muxed = (np->logic_type == ir::LogicType::Mux);
                                        return info;
                                    }
                                }
                                // Also check Net predecessors of the Combinational
                                // node (one more level) for clock sources.
                                for (uint64_t net_pred_id : graph.predecessors(np_id)) {
                                    const ir::Node* net_pred = graph.find_node(net_pred_id);
                                    if (!net_pred || net_pred->kind != ir::NodeKind::Net)
                                        continue;
                                    for (uint64_t pp_id : graph.predecessors(net_pred_id)) {
                                        const ir::Node* pp = graph.find_node(pp_id);
                                        if (pp && pp->kind == ir::NodeKind::Port &&
                                            is_clock_port(pp->hier_name, graph)) {
                                            info.root_clock =
                                                trace_to_top_port(pp->hier_name, graph);
                                            info.is_gated = (np->logic_type == ir::LogicType::And);
                                            info.is_muxed = (np->logic_type == ir::LogicType::Mux);
                                            return info;
                                        }
                                    }
                                }
                            }
                            if (np->kind == ir::NodeKind::Net) {
                                cur = np;
                                found = true;
                                break;
                            }
                        }
                        if (!found)
                            break;
                    }
                }
            }
        }
    }

    for (const auto& node : graph.nodes()) {
        if (node.kind == ir::NodeKind::Register && node.clock_domain == clock_name &&
            !node.root_clock.empty()) {
            info.root_clock = node.root_clock;
            info.is_gated = node.clock_is_gated;
            info.is_muxed = node.clock_is_muxed;
            return info;
        }
    }

    info.root_clock.clear();
    return info;
}

ResolveResult ClockResolver::resolve(const ir::Graph& graph) {
    ResolveResult result;

    // Reset mutable caches so reuse on a second graph cannot return stale data.
    port_names_.clear();
    short_port_names_.clear();
    port_names_built_ = false;
    port_by_name_.clear();
    short_to_hier_.clear();
    port_index_built_ = false;

    std::unordered_set<std::string> seen_clocks;
    for (const auto& node : graph.nodes()) {
        if (node.kind != ir::NodeKind::Register)
            continue;
        if (node.clock_domain.empty() || node.clock_domain == "unknown")
            continue;

        if (seen_clocks.count(node.clock_domain))
            continue;
        seen_clocks.insert(node.clock_domain);

        ClockInfo info = trace_clock(node.clock_domain, graph);
        result.clock_map[node.clock_domain] = std::move(info);
    }

    for (const auto& node : graph.nodes()) {
        if (node.kind != ir::NodeKind::Register)
            continue;
        if (node.clock_domain.empty() || node.clock_domain == "unknown")
            continue;

        auto it = result.clock_map.find(node.clock_domain);
        if (it == result.clock_map.end())
            continue;

        const ClockInfo& ci = it->second;
        if (ci.is_muxed) {
            result.warnings.push_back("Clock '" + node.clock_domain +
                                      "' is derived from multiple sources: "
                                      "analysis requires user annotation.");
        }
    }

    return result;
}

}  // namespace opencdc::clock
