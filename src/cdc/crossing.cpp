#include "cdc/crossing.h"
#include "util/parallel.h"
#include <unordered_set>
#include <algorithm>
#include <cctype>

namespace opencdc::cdc {

bool CrossingAnalyzer::is_safe_multi_bit_crossing(uint64_t src_id, uint64_t dst_id,
                                                   const ir::Graph& graph) const {
    const ir::Node* src = graph.find_node(src_id);
    const ir::Node* dst = graph.find_node(dst_id);

    if (!src || !dst) return false;

    if (src->width <= 1) return true;

    if (pattern_recognizer_) {
        if (pattern_recognizer_->is_verified_safe_crossing(src_id, dst_id, graph))
            return true;
    }

    return false;
}

const clock::ClockDomain* CrossingAnalyzer::find_domain_for_node(
    uint64_t node_id,
    const std::vector<clock::ClockDomain>& domains,
    const std::unordered_map<uint64_t, size_t>& register_to_domain) const {
    auto it = register_to_domain.find(node_id);
    if (it == register_to_domain.end()) return nullptr;
    if (it->second >= domains.size()) return nullptr;
    return &domains[it->second];
}

std::string CrossingAnalyzer::build_reason(const Finding& f) const {
    std::string r = "Register '" + f.source_reg_name + "' in domain '" +
                    f.source_domain + "' drives register '" + f.dest_reg_name +
                    "' in domain '" + f.dest_domain +
                    "' without synchronization.";

    if (f.detected_sync == SyncPattern::None) {
        r += " No 2FF/3FF synchronizer chain detected on destination side.";
    }
    return r;
}

std::vector<Finding> CrossingAnalyzer::analyze(
    const ir::Graph& graph,
    const std::vector<clock::ClockDomain>& domains,
    const std::unordered_map<uint64_t, size_t>& register_to_domain,
    size_t num_threads) {

    // Collect all source register IDs for parallel processing
    std::vector<uint64_t> source_ids;
    for (const auto& node : graph.nodes()) {
        if (node.kind == ir::NodeKind::Register) {
            source_ids.push_back(node.id);
        }
    }

    auto process_register = [&](uint64_t src_id) -> std::vector<Finding> {
        std::vector<Finding> local_findings;
        const ir::Node* src = graph.find_node(src_id);
        if (!src || src->kind != ir::NodeKind::Register) return local_findings;

        const clock::ClockDomain* src_dom =
            find_domain_for_node(src_id, domains, register_to_domain);
        if (!src_dom) return local_findings;

        auto path_result = graph.find_register_paths(src_id);
        if (path_result.truncated) {
            Finding trunc;
            trunc.rule_id = "CDC010";
            trunc.rule_name = "path_traversal_truncated";
            trunc.severity = "warning";
            trunc.source_reg_id = src_id;
            trunc.source_reg_name = src->hier_name;
            trunc.source_domain = src_dom->name;
            trunc.source_loc = src->loc;
            trunc.bus_width = src->width;
            trunc.source_module_path = src->module_path;
            trunc.reason = "Path traversal truncated at " + std::to_string(path_result.max_paths) +
                          " paths from register '" + src->hier_name + "'.";
            local_findings.push_back(std::move(trunc));
        }

        std::unordered_set<std::string> seen;

        for (const auto& reg_path : path_result.paths) {
            uint64_t dst_id = reg_path.dst_reg_id;
            const ir::Node* dst = graph.find_node(dst_id);
            if (!dst || dst->kind != ir::NodeKind::Register) continue;

            const clock::ClockDomain* dst_dom =
                find_domain_for_node(dst_id, domains, register_to_domain);
            if (!dst_dom) continue;

            if (src_dom->id == dst_dom->id) continue;

            std::string key = std::to_string(src_id) + "->" + std::to_string(dst_id);
            if (seen.count(key)) continue;
            seen.insert(key);

            bool is_false_path = false;
            if (clock_constraints_) {
                clock::PathMatchContext ctx;
                ctx.source_clock = src_dom->name;
                ctx.destination_clock = dst_dom->name;
                ctx.source_register = src->hier_name;
                ctx.destination_register = dst->hier_name;
                ctx.source_cell = src->hier_name;
                ctx.destination_cell = dst->hier_name;
                ctx.source_pin = src->hier_name;
                ctx.destination_pin = dst->hier_name;
                ctx.path_nodes.clear();
                for (auto nid : reg_path.node_ids) {
                    const ir::Node* n = graph.find_node(nid);
                    if (n) ctx.path_nodes.push_back(n->hier_name);
                }
                is_false_path = clock_constraints_->is_false_path(ctx);
            }
            if (is_false_path) continue;

            Finding f;
            f.rule_id = "CDC001";
            f.rule_name = "unsynchronized_crossing";
            f.severity = "error";
            f.source_reg_id = src_id;
            f.dest_reg_id = dst_id;
            f.source_reg_name = src->hier_name;
            f.dest_reg_name = dst->hier_name;
            f.source_domain = src_dom->name;
            f.dest_domain = dst_dom->name;
            f.path.node_ids = reg_path.node_ids;
            f.source_loc = src->loc;
            f.detected_sync = sync_matcher_.find_pattern_for_dest(dst_id, graph);
            f.bus_width = src->width;
            f.source_module_path = src->module_path;
            f.dest_module_path = dst->module_path;
            f.crosses_module_boundary = !src->module_path.empty() &&
                                        !dst->module_path.empty() &&
                                        src->module_path != dst->module_path;

            SyncPattern crossing_sync = f.detected_sync;

            f.is_gray_coded = (pattern_recognizer_ &&
                              (pattern_recognizer_->is_gray_coded(src_id, graph) ||
                               pattern_recognizer_->is_gray_coded(dst_id, graph))) ||
                              src->is_gray_coded || dst->is_gray_coded;

            f.has_handshake = (pattern_recognizer_ &&
                              (pattern_recognizer_->is_handshake_signal(src_id, graph) ||
                               pattern_recognizer_->is_handshake_signal(dst_id, graph))) ||
                              src->is_handshake_signal || dst->is_handshake_signal;

            if (crossing_sync != SyncPattern::None) {
                f.severity = "warning";
            }

            f.reason = build_reason(f);

            if (clock_constraints_) {
                for (const auto& mcp : clock_constraints_->multi_cycle_paths) {
                    if ((!mcp.from_clock.empty() && !mcp.to_clock.empty()) &&
                        (clock::pattern_matches(mcp.from_clock, src_dom->name) &&
                         clock::pattern_matches(mcp.to_clock, dst_dom->name))) {
                        f.has_multicycle_exception = true;
                        f.multicycle_cycles = mcp.cycles;
                        f.constraint_source = "multicycle_path: " + mcp.from_clock +
                                              " -> " + mcp.to_clock +
                                              " (" + std::to_string(mcp.cycles) + " cycles)";
                        break;
                    }
                }
            }

            local_findings.push_back(std::move(f));

            // NOTE: no early continue here — CDC002/004/005/007 are
            // independent of synchronizer presence. A synchronized crossing
            // can still be a multi-bit hazard (per-bit skew), and gated /
            // muxed / reset properties apply regardless of sync chains.

            bool safe_crossing = is_safe_multi_bit_crossing(src_id, dst_id, graph);

            if (src->width > 1 && !safe_crossing) {
                Finding mb;
                mb.rule_id = "CDC002";
                mb.rule_name = "multi_bit_crossing";
                mb.severity = "error";
                mb.source_reg_id = src_id;
                mb.dest_reg_id = dst_id;
                mb.source_reg_name = src->hier_name;
                mb.dest_reg_name = dst->hier_name;
                mb.source_domain = src_dom->name;
                mb.dest_domain = dst_dom->name;
                mb.path.node_ids = reg_path.node_ids;
                mb.source_loc = src->loc;
                mb.detected_sync = f.detected_sync;
                mb.bus_width = src->width;
                mb.is_gray_coded = (pattern_recognizer_ &&
                                    (pattern_recognizer_->is_gray_coded(src_id, graph) ||
                                     pattern_recognizer_->is_gray_coded(dst_id, graph))) ||
                                   src->is_gray_coded || dst->is_gray_coded;
                mb.has_handshake = (pattern_recognizer_ &&
                                    (pattern_recognizer_->is_handshake_signal(src_id, graph) ||
                                     pattern_recognizer_->is_handshake_signal(dst_id, graph))) ||
                                   src->is_handshake_signal || dst->is_handshake_signal;
                mb.source_module_path = src->module_path;
                mb.dest_module_path = dst->module_path;
                mb.crosses_module_boundary = f.crosses_module_boundary;
                mb.reason = "Multi-bit bus '" + src->hier_name +
                            "' (width=" + std::to_string(src->width) +
                            ") crosses from domain '" + src_dom->name +
                            "' to domain '" + dst_dom->name +
                            "' without gray-code encoding or handshake protocol.";
                local_findings.push_back(std::move(mb));
            }

            if (src->clock_is_gated || dst->clock_is_gated) {
                const ir::Node* gated_src = src->clock_is_gated ? src : nullptr;
                const ir::Node* gated_dst = dst->clock_is_gated ? dst : nullptr;
                Finding gc;
                gc.rule_id = "CDC004";
                gc.rule_name = "gated_clock_crossing";
                gc.severity = "warning";
                gc.source_reg_id = src_id;
                gc.dest_reg_id = dst_id;
                gc.source_reg_name = src->hier_name;
                gc.dest_reg_name = dst->hier_name;
                gc.source_domain = src_dom->name;
                gc.dest_domain = dst_dom->name;
                gc.path.node_ids = reg_path.node_ids;
                gc.source_loc = src->loc;
                gc.bus_width = src->width;
                gc.source_module_path = src->module_path;
                gc.dest_module_path = dst->module_path;
                std::string gated_info;
                if (gated_src && gated_dst) {
                    gated_info = "both source '" + gated_src->hier_name +
                                 "' (domain '" + gated_src->clock_domain +
                                 "') and destination '" + gated_dst->hier_name +
                                 "' (domain '" + gated_dst->clock_domain + "')";
                } else {
                    const ir::Node* g = gated_src ? gated_src : gated_dst;
                    gated_info = "'" + g->hier_name + "' (domain '" + g->clock_domain +
                                 "', root: '" + g->root_clock + "')";
                }
                gc.reason = "Register " + gated_info +
                            " is clocked by a gated clock on a crossing from domain '" +
                            src_dom->name + "' to domain '" + dst_dom->name + "'.";
                local_findings.push_back(std::move(gc));
            }

            bool src_muxed_no_reset = src->clock_is_muxed && src->reset_signal.empty();
            bool dst_muxed_no_reset = dst->clock_is_muxed && dst->reset_signal.empty();
            if (src_muxed_no_reset || dst_muxed_no_reset) {
                const ir::Node* muxed_node = src_muxed_no_reset ? src : dst;
                Finding mr;
                mr.rule_id = "CDC005";
                mr.rule_name = "muxed_clock_no_reset";
                mr.severity = "warning";
                mr.source_reg_id = src_id;
                mr.dest_reg_id = dst_id;
                mr.source_reg_name = src->hier_name;
                mr.dest_reg_name = dst->hier_name;
                mr.source_domain = src_dom->name;
                mr.dest_domain = dst_dom->name;
                mr.path.node_ids = reg_path.node_ids;
                mr.source_loc = src->loc;
                mr.bus_width = src->width;
                mr.source_module_path = src->module_path;
                mr.dest_module_path = dst->module_path;
                mr.reason = "Register '" + muxed_node->hier_name +
                            "' is clocked by muxed clock '" + muxed_node->clock_domain +
                            "' without reset signal.";
                local_findings.push_back(std::move(mr));
            }

            if (src->reset_signal.empty() || dst->reset_signal.empty()) {
                Finding nr;
                nr.rule_id = "CDC007";
                nr.rule_name = "missing_reset";
                nr.severity = "warning";
                nr.source_reg_id = src_id;
                nr.dest_reg_id = dst_id;
                nr.source_reg_name = src->hier_name;
                nr.dest_reg_name = dst->hier_name;
                nr.source_domain = src_dom->name;
                nr.dest_domain = dst_dom->name;
                nr.path.node_ids = reg_path.node_ids;
                nr.source_loc = src->loc;
                nr.bus_width = src->width;
                nr.source_module_path = src->module_path;
                nr.dest_module_path = dst->module_path;
                std::string missing = (src->reset_signal.empty() && dst->reset_signal.empty())
                                          ? "neither register has"
                                          : "'" + std::string(src->reset_signal.empty()
                                                                  ? src->hier_name
                                                                  : dst->hier_name) + "' lacks";
                nr.reason = "CDC crossing between registers '" + src->hier_name +
                            "' and '" + dst->hier_name +
                            "': " + missing + " a reset signal.";
                local_findings.push_back(std::move(nr));
            }
        }
        return local_findings;
    };

    // Pre-compute pattern caches on the main thread before parallel analysis
    // to avoid data races on mutable cache members.
    if (pattern_recognizer_) {
        pattern_recognizer_->ensure_patterns(graph);
    }

    // Parallel per-register analysis
    std::vector<std::vector<Finding>> per_register;
    if (num_threads <= 1) {
        per_register.reserve(source_ids.size());
        for (auto id : source_ids) {
            per_register.push_back(process_register(id));
        }
    } else {
        per_register = util::parallel_map<uint64_t, decltype(process_register), std::vector<Finding>>(
            source_ids, process_register, num_threads);
    }

    // Merge
    std::vector<Finding> findings;
    for (auto& batch : per_register) {
        for (auto& f : batch) {
            findings.push_back(std::move(f));
        }
    }

    // Daisy-chain detection (sequential — needs global visited set)
    std::unordered_set<uint64_t> reported_sources;
    for (const auto& src : graph.nodes()) {
        if (src.kind != ir::NodeKind::Register) continue;
        if (reported_sources.count(src.id)) continue;

        const clock::ClockDomain* src_dom =
            find_domain_for_node(src.id, domains, register_to_domain);
        if (!src_dom) continue;

        std::vector<uint64_t> domain_chain;
        domain_chain.push_back(src.id);
        std::unordered_set<uint64_t> visited;
        std::unordered_set<size_t> visited_domain_ids;
        visited.insert(src.id);
        visited_domain_ids.insert(src_dom->id);

        std::vector<uint64_t> stack;
        stack.push_back(src.id);

        while (!stack.empty()) {
            uint64_t node_id = stack.back();
            stack.pop_back();
            for (uint64_t succ : graph.register_successors(node_id)) {
                if (visited.count(succ)) continue;
                const ir::Node* succ_node = graph.find_node(succ);
                if (!succ_node || succ_node->kind != ir::NodeKind::Register) continue;
                const clock::ClockDomain* succ_dom =
                    find_domain_for_node(succ, domains, register_to_domain);
                if (!succ_dom) continue;
                if (!visited_domain_ids.count(succ_dom->id)) {
                    domain_chain.push_back(succ);
                    visited_domain_ids.insert(succ_dom->id);
                }
                visited.insert(succ);
                stack.push_back(succ);
            }
        }

        reported_sources.insert(src.id);

        if (domain_chain.size() >= 3) {
            Finding dc;
            dc.rule_id = "CDC008";
            dc.rule_name = "multi_domain_daisy_chain";
            dc.severity = "warning";
            dc.source_reg_id = src.id;
            dc.source_reg_name = src.hier_name;
            dc.source_domain = src_dom->name;
            dc.source_loc = src.loc;
            dc.bus_width = src.width;
            std::string chain_desc;
            for (size_t i = 0; i < domain_chain.size(); ++i) {
                const ir::Node* n = graph.find_node(domain_chain[i]);
                if (n) {
                    if (!chain_desc.empty()) chain_desc += " -> ";
                    chain_desc += n->hier_name;
                }
            }
            dc.reason = "Register '" + src.hier_name +
                        "' is part of a daisy chain crossing " +
                        std::to_string(domain_chain.size() - 1) +
                        " clock domains: " + chain_desc + ".";
            findings.push_back(std::move(dc));
        }
    }

    return findings;
}

} // namespace opencdc::cdc
