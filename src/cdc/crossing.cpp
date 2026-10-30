#include "cdc/crossing.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>

#include "util/parallel.h"

namespace opencdc::cdc {

bool CrossingAnalyzer::is_path_through_safe_blackbox(
    const ir::Graph& graph, const std::vector<uint64_t>& path_node_ids) const {
    if (!blackbox_registry_)
        return false;

    for (uint64_t nid : path_node_ids) {
        const ir::Node* node = graph.find_node(nid);
        if (!node)
            continue;
        if (!node->module_type.empty()) {
            const BlackBoxModel* by_type = blackbox_registry_->find(node->module_type);
            if (by_type && by_type->properties.is_safe_crossing)
                return true;
            continue;
        }
        std::string mp = node->module_path;
        const BlackBoxModel* model = blackbox_registry_->find(mp);
        if (model && model->properties.is_safe_crossing)
            return true;
        auto slash = mp.find_last_of("/.");
        if (slash != std::string::npos) {
            std::string leaf = mp.substr(slash + 1);
            model = blackbox_registry_->find(leaf);
            if (model && model->properties.is_safe_crossing)
                return true;
        }
    }
    return false;
}

bool CrossingAnalyzer::is_safe_multi_bit_crossing(uint64_t src_id, uint64_t dst_id,
                                                  const ir::Graph& graph, bool wire_path) const {
    const ir::Node* src = graph.find_node(src_id);
    const ir::Node* dst = graph.find_node(dst_id);

    if (!src || !dst)
        return false;

    if (src->width <= 1)
        return true;

    if (pattern_recognizer_) {
        if (pattern_recognizer_->is_verified_safe_crossing(src_id, dst_id, graph, wire_path))
            return true;
    }

    return false;
}

const clock::ClockDomain* CrossingAnalyzer::find_domain_for_node(
    uint64_t node_id, const std::vector<clock::ClockDomain>& domains,
    const std::unordered_map<uint64_t, size_t>& register_to_domain) const {
    auto it = register_to_domain.find(node_id);
    if (it == register_to_domain.end())
        return nullptr;
    if (it->second >= domains.size())
        return nullptr;
    return &domains[it->second];
}

std::string CrossingAnalyzer::build_reason(const Finding& f) const {
    std::string r = "Register '" + f.source_reg_name + "' in domain '" + f.source_domain +
                    "' drives register '" + f.dest_reg_name + "' in domain '" + f.dest_domain +
                    "' without synchronization.";

    if (f.detected_sync == SyncPattern::None) {
        r += " No 2FF/3FF synchronizer chain detected on destination side.";
    }
    return r;
}

// The if/case arms that load a node: its own, and, for a register capturing
// a next-state net (`q <= q_d`), those of the nets in front of it.
static std::vector<std::string> load_branches_of(const ir::Graph& graph, uint64_t id) {
    std::vector<std::string> out;
    const ir::Node* n = graph.find_node(id);
    for (int hop = 0; n && hop < 8; ++hop) {
        out.insert(out.end(), n->load_branches.begin(), n->load_branches.end());
        if (hop > 0 && !n->load_branches.empty())
            break;
        const ir::Node* next = nullptr;
        for (uint64_t p : graph.predecessors(n->id)) {
            if (p == n->id || graph.edge_role(p, n->id) != ir::EdgeRole::Data)
                continue;
            const ir::Node* pn = graph.find_node(p);
            if (!pn || (pn->kind != ir::NodeKind::Net && pn->kind != ir::NodeKind::Port))
                continue;
            if (next)
                return out;  // more than one data input: no next-state net
            next = pn;
        }
        n = next;
    }
    return out;
}

const ir::Node* CrossingAnalyzer::handshake_qualifier(
    uint64_t src_id, uint64_t dst_id, const ir::Graph& graph,
    const std::unordered_map<uint64_t, size_t>& register_to_domain,
    const ir::Node*& request) const {
    auto src_branches = load_branches_of(graph, src_id);
    if (src_branches.empty())
        return nullptr;
    auto domain_of = [&](uint64_t id) -> long {
        auto it = register_to_domain.find(id);
        return it == register_to_domain.end() ? -1 : static_cast<long>(it->second);
    };
    long src_dom = domain_of(src_id);
    long dst_dom = domain_of(dst_id);

    // Registers of dst's clock that gate dst through a condition: backwards
    // from dst over nets, ports and logic, with a Control edge on the way.
    std::vector<uint64_t> gates;
    std::unordered_set<uint64_t> seen;
    std::vector<std::pair<uint64_t, bool>> stack;
    for (uint64_t p : graph.predecessors(dst_id))
        stack.push_back({p, graph.edge_role(p, dst_id) == ir::EdgeRole::Control});
    while (!stack.empty() && seen.size() < 4096) {
        auto [id, control] = stack.back();
        stack.pop_back();
        if (!seen.insert(id * 2 + (control ? 1 : 0)).second)
            continue;
        const ir::Node* n = graph.find_node(id);
        if (!n || id == dst_id)
            continue;
        if (n->kind == ir::NodeKind::Register) {
            if (control && domain_of(id) == dst_dom)
                gates.push_back(id);
            continue;
        }
        for (uint64_t p : graph.predecessors(id))
            stack.push_back({p, control || graph.edge_role(p, id) == ir::EdgeRole::Control});
    }

    // A gate that is a synchronizer stage past the first, whose chain is fed
    // by a register of src's clock loaded in one of src's arms: the request.
    for (uint64_t q : gates) {
        std::vector<std::pair<uint64_t, int>> back{{q, 0}};
        std::unordered_set<uint64_t> visited{q};
        while (!back.empty()) {
            auto [e, depth] = back.back();
            back.pop_back();
            const ir::Node* en = graph.find_node(e);
            if (!en)
                continue;
            bool past_first = depth > 0 || en->packed_shift_depth >= 2;
            if (past_first &&
                sync_matcher_.find_pattern_for_dest(e, graph, false) != SyncPattern::None) {
                for (uint64_t r : graph.register_predecessors(e, false, true)) {
                    if (r == src_id || domain_of(r) != src_dom)
                        continue;
                    for (const auto& b : load_branches_of(graph, r)) {
                        if (std::find(src_branches.begin(), src_branches.end(), b) !=
                            src_branches.end()) {
                            request = graph.find_node(r);
                            return graph.find_node(q);
                        }
                    }
                }
            }
            if (depth >= 4)
                continue;
            for (uint64_t p : graph.register_predecessors(e, false, true)) {
                if (domain_of(p) == dst_dom && visited.insert(p).second)
                    back.push_back({p, depth + 1});
            }
        }
    }
    return nullptr;
}

std::vector<Finding> CrossingAnalyzer::analyze(
    const ir::Graph& graph, const std::vector<clock::ClockDomain>& domains,
    const std::unordered_map<uint64_t, size_t>& register_to_domain, size_t num_threads) {
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
        if (!src || src->kind != ir::NodeKind::Register)
            return local_findings;

        const clock::ClockDomain* src_dom =
            find_domain_for_node(src_id, domains, register_to_domain);
        if (!src_dom)
            return local_findings;

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
            trunc.reason =
                (path_result.truncated_by_depth
                     ? "Path traversal truncated at depth " + std::to_string(ir::MAX_PATH_DEPTH)
                     : "Path traversal truncated at " + std::to_string(path_result.max_paths) +
                           " paths") +
                " from register '" + src->hier_name + "'.";
            trunc.safety_status = SafetyStatus::Ambiguous;
            trunc.safety_provenance = "Path traversal truncated — some crossings may be missed";
            local_findings.push_back(std::move(trunc));
        }

        std::unordered_set<std::string> seen;

        for (const auto& reg_path : path_result.paths) {
            uint64_t dst_id = reg_path.dst_reg_id;
            const ir::Node* dst = graph.find_node(dst_id);
            if (!dst || dst->kind != ir::NodeKind::Register)
                continue;

            const clock::ClockDomain* dst_dom =
                find_domain_for_node(dst_id, domains, register_to_domain);
            if (!dst_dom)
                continue;

            if (src_dom->id == dst_dom->id)
                continue;

            std::string key = std::to_string(src_id) + "->" + std::to_string(dst_id);
            if (seen.count(key))
                continue;
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
                    if (n)
                        ctx.path_nodes.push_back(n->hier_name);
                }
                is_false_path = clock_constraints_->is_false_path(ctx);
            }
            if (is_false_path) {
                // Record false-path suppression for auditability.
                Finding sup;
                sup.rule_id = "CDC001";
                sup.rule_name = "unsynchronized_crossing";
                sup.severity = "info";
                sup.source_reg_id = src_id;
                sup.dest_reg_id = dst_id;
                sup.source_reg_name = src->hier_name;
                sup.dest_reg_name = dst->hier_name;
                sup.source_domain = src_dom->name;
                sup.dest_domain = dst_dom->name;
                sup.path.node_ids = reg_path.node_ids;
                sup.source_loc = src->loc;
                sup.bus_width = src->width;
                sup.source_module_path = src->module_path;
                sup.dest_module_path = dst->module_path;
                sup.suppressed_by_false_path = true;
                sup.false_path_source = "false_path constraint";
                sup.reason = "Crossing suppressed by false-path constraint from '" +
                             src->hier_name + "' to '" + dst->hier_name + "'.";
                sup.safety_status = SafetyStatus::Ambiguous;
                sup.safety_provenance = "Suppressed by false-path constraint";
                local_findings.push_back(std::move(sup));
                continue;
            }

            // Blackbox suppression: safe black box on path handles sync.
            if (is_path_through_safe_blackbox(graph, reg_path.node_ids)) {
                Finding bb;
                bb.rule_id = "CDC001";
                bb.rule_name = "unsynchronized_crossing";
                bb.severity = "info";
                bb.source_reg_id = src_id;
                bb.dest_reg_id = dst_id;
                bb.source_reg_name = src->hier_name;
                bb.dest_reg_name = dst->hier_name;
                bb.source_domain = src_dom->name;
                bb.dest_domain = dst_dom->name;
                bb.path.node_ids = reg_path.node_ids;
                bb.source_loc = src->loc;
                bb.bus_width = src->width;
                bb.source_module_path = src->module_path;
                bb.dest_module_path = dst->module_path;
                bb.reason =
                    "Crossing suppressed: path passes through safe black box "
                    "module with built-in synchronization.";
                bb.safety_status = SafetyStatus::VerifiedSafe;
                bb.safety_provenance = "Path through safe black box module";
                local_findings.push_back(std::move(bb));
                continue;
            }

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
            // A gray-coded source changes one bit per cycle, so a chain of
            // destination-wide stages synchronizes it (async-FIFO pointers) -
            // if the code arrives unchanged: only nets and ports between the
            // registers. Logic on the path (a gray-to-binary decoder) makes
            // the crossing a plain multi-bit one.
            bool wire_path = true;
            for (size_t i = 1; i + 1 < reg_path.node_ids.size(); ++i) {
                const ir::Node* n = graph.find_node(reg_path.node_ids[i]);
                if (!n || (n->kind != ir::NodeKind::Net && n->kind != ir::NodeKind::Port)) {
                    wire_path = false;
                    break;
                }
            }
            const bool gray_source =
                wire_path &&
                (src->is_gray_coded ||
                 (pattern_recognizer_ && pattern_recognizer_->is_gray_coded(src_id, graph)));
            f.detected_sync = sync_matcher_.find_pattern_for_dest(dst_id, graph, true, gray_source);
            f.bus_width = src->width;
            f.source_module_path = src->module_path;
            f.dest_module_path = dst->module_path;
            f.crosses_module_boundary = !src->module_path.empty() && !dst->module_path.empty() &&
                                        src->module_path != dst->module_path;

            // Evidence chain for explainability (golden corpus expects clk_a/clk_b).
            f.evidence_chain.push_back(
                EvidenceStep{"clock_domains",
                             "Clock domains: '" + src_dom->name + "' -> '" + dst_dom->name + "'",
                             "identified", src->loc.file});
            f.evidence_chain.push_back(EvidenceStep{"bus_width",
                                                    "Bus width: " + std::to_string(src->width),
                                                    "measured", src->loc.file});

            SyncPattern crossing_sync = f.detected_sync;

            f.is_gray_coded =
                gray_source || dst->is_gray_coded ||
                (pattern_recognizer_ && pattern_recognizer_->is_gray_coded(dst_id, graph));

            f.has_handshake = (pattern_recognizer_ &&
                               (pattern_recognizer_->is_handshake_signal(src_id, graph) ||
                                pattern_recognizer_->is_handshake_signal(dst_id, graph))) ||
                              src->is_handshake_signal || dst->is_handshake_signal;

            const ir::Node* handshake_request = nullptr;
            const ir::Node* handshake_gate =
                crossing_sync == SyncPattern::None
                    ? handshake_qualifier(src_id, dst_id, graph, register_to_domain,
                                          handshake_request)
                    : nullptr;

            if (crossing_sync != SyncPattern::None &&
                sync_matcher_.below_min_stages(dst_id, graph, min_sync_stages_, gray_source)) {
                f.severity = "error";
                f.safety_status = SafetyStatus::VerifiedUnsafe;
                f.safety_provenance = "synchronizer chain shorter than min_sync_stages=" +
                                      std::to_string(min_sync_stages_);
            } else if (crossing_sync != SyncPattern::None &&
                       sync_matcher_.has_chain_warnings(graph, dst_id, true, gray_source)) {
                f.severity = "warning";
                f.safety_status = SafetyStatus::Ambiguous;
                f.safety_provenance =
                    "synchronizer chain detected but has structural warnings "
                    "(reset mismatch or stage fanout)";
            } else if (crossing_sync != SyncPattern::None) {
                f.severity = "info";
                f.safety_status = SafetyStatus::VerifiedSafe;
                f.safety_provenance = "synchronizer chain detected at destination";
            } else if (handshake_gate) {
                f.severity = "info";
                f.safety_status = SafetyStatus::VerifiedSafe;
                f.safety_provenance =
                    "handshake payload: loaded with request '" + handshake_request->hier_name +
                    "', used under its synchronized copy '" + handshake_gate->hier_name + "'";
            } else {
                f.safety_status = SafetyStatus::VerifiedUnsafe;
                f.safety_provenance = "No synchronizer chain detected on destination side";
                f.propagates_uncertainty = true;
                f.uncertainty_reason = "Unsynchronized crossing propagates unknown value";
            }

            f.reason = build_reason(f);

            // Multicycle suppression when policy allows: emit info audit trail
            // instead of error, otherwise just annotate. Supports partial
            // constraints (from-only or to-only) as well as full pairs.
            bool multicycle_suppressed = false;
            if (clock_constraints_) {
                for (const auto& mcp : clock_constraints_->multi_cycle_paths) {
                    bool from_ok = mcp.from_clock.empty() ||
                                   clock::pattern_matches(mcp.from_clock, src_dom->name);
                    bool to_ok =
                        mcp.to_clock.empty() || clock::pattern_matches(mcp.to_clock, dst_dom->name);
                    if (!from_ok || !to_ok)
                        continue;
                    if (mcp.from_clock.empty() && mcp.to_clock.empty())
                        continue;
                    f.has_multicycle_exception = true;
                    f.multicycle_cycles = mcp.cycles;
                    f.constraint_source = "multicycle_path: " + mcp.from_clock + " -> " +
                                          mcp.to_clock + " (" + std::to_string(mcp.cycles) +
                                          " cycles)";
                    if (multicycle_policy_ && multicycle_policy_->suppress_findings) {
                        bool suppress = false;
                        for (const auto& r : multicycle_policy_->suppress_rules) {
                            if (r == "CDC001") {
                                suppress = true;
                                break;
                            }
                        }
                        if (suppress) {
                            f.severity = "info";
                            f.suppressed_by_multicycle = true;
                            f.multicycle_source = f.constraint_source;
                            f.safety_status = SafetyStatus::Ambiguous;
                            f.safety_provenance = "Suppressed by multicycle path constraint";
                            multicycle_suppressed = true;
                        }
                    }
                    break;
                }
            }

            local_findings.push_back(std::move(f));
            if (multicycle_suppressed) {
                continue;
            }

            // NOTE: no early continue here — CDC002/004/005/007 are
            // independent of synchronizer presence. A synchronized crossing
            // can still be a multi-bit hazard (per-bit skew), and gated /
            // muxed / reset properties apply regardless of sync chains.

            // A handshake payload is held while it crosses: no bit skew.
            bool safe_crossing =
                handshake_gate || is_safe_multi_bit_crossing(src_id, dst_id, graph, wire_path);

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
                mb.is_gray_coded = f.is_gray_coded;
                mb.has_handshake = (pattern_recognizer_ &&
                                    (pattern_recognizer_->is_handshake_signal(src_id, graph) ||
                                     pattern_recognizer_->is_handshake_signal(dst_id, graph))) ||
                                   src->is_handshake_signal || dst->is_handshake_signal;
                mb.source_module_path = src->module_path;
                mb.dest_module_path = dst->module_path;
                mb.crosses_module_boundary = f.crosses_module_boundary;
                mb.reason = "Multi-bit bus '" + src->hier_name +
                            "' (width=" + std::to_string(src->width) + ") crosses from domain '" +
                            src_dom->name + "' to domain '" + dst_dom->name +
                            "' without gray-code encoding or handshake protocol.";
                local_findings.push_back(std::move(mb));
            }

            // CDC004: gated clocks on endpoints or intermediates along the path.
            const ir::Node* gated_hit = nullptr;
            if (src->clock_is_gated)
                gated_hit = src;
            else if (dst->clock_is_gated)
                gated_hit = dst;
            else {
                for (size_t k = 1; k + 1 < reg_path.node_ids.size(); ++k) {
                    const ir::Node* n = graph.find_node(reg_path.node_ids[k]);
                    if (n && n->kind == ir::NodeKind::Register && n->clock_is_gated) {
                        gated_hit = n;
                        break;
                    }
                }
            }
            if (gated_hit) {
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
                if (src->clock_is_gated && dst->clock_is_gated) {
                    gated_info = "both source '" + src->hier_name + "' (domain '" +
                                 src->clock_domain + "') and destination '" + dst->hier_name +
                                 "' (domain '" + dst->clock_domain + "')";
                } else {
                    gated_info = "'" + gated_hit->hier_name + "' (domain '" +
                                 gated_hit->clock_domain + "', root: '" + gated_hit->root_clock +
                                 "')";
                }
                gc.reason = "Register " + gated_info +
                            " is clocked by a gated clock on a crossing from domain '" +
                            src_dom->name + "' to domain '" + dst_dom->name + "'.";
                local_findings.push_back(std::move(gc));
            }

            bool src_muxed_no_reset = src->clock_is_muxed && src->reset_signal.empty();
            bool dst_muxed_no_reset = dst->clock_is_muxed && dst->reset_signal.empty();
            const ir::Node* muxed_hit = nullptr;
            if (src_muxed_no_reset)
                muxed_hit = src;
            else if (dst_muxed_no_reset)
                muxed_hit = dst;
            else {
                for (size_t k = 1; k + 1 < reg_path.node_ids.size(); ++k) {
                    const ir::Node* n = graph.find_node(reg_path.node_ids[k]);
                    if (n && n->kind == ir::NodeKind::Register && n->clock_is_muxed &&
                        n->reset_signal.empty()) {
                        muxed_hit = n;
                        break;
                    }
                }
            }
            if (muxed_hit) {
                const ir::Node* muxed_node = muxed_hit;
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
                mr.reason = "Register '" + muxed_node->hier_name + "' is clocked by muxed clock '" +
                            muxed_node->clock_domain + "' without reset signal.";
                local_findings.push_back(std::move(mr));
            }

            if (src->reset_signal.empty() || dst->reset_signal.empty()) {
                Finding nr;
                nr.rule_id = "CDC007";
                nr.rule_name = "missing_reset";
                // Strict reset policy escalates to warning only when NEITHER
                // has reset; mixed (one side reset) stays advisory info.
                bool both_unreset = src->reset_signal.empty() && dst->reset_signal.empty();
                bool strict_reset = reset_policy_ && reset_policy_->require_cdc_register_reset;
                nr.severity = (strict_reset && both_unreset) ? "warning" : "info";
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
                std::string missing =
                    (src->reset_signal.empty() && dst->reset_signal.empty())
                        ? "neither register has"
                        : "'" +
                              std::string(src->reset_signal.empty() ? src->hier_name
                                                                    : dst->hier_name) +
                              "' lacks";
                nr.reason = "CDC crossing between registers '" + src->hier_name + "' and '" +
                            dst->hier_name + "': " + missing + " a reset signal.";
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
        per_register =
            util::parallel_map<uint64_t, decltype(process_register), std::vector<Finding>>(
                source_ids, process_register, num_threads);
    }

    // Merge
    std::vector<Finding> findings;
    for (auto& batch : per_register) {
        for (auto& f : batch) {
            findings.push_back(std::move(f));
        }
    }

    // Daisy-chain detection: sequential A->B->C ordering along a single
    // path, not parallel fanout. DFS tracks the current path's domain
    // sequence; only a path visiting >=3 distinct domains reports CDC008.
    // Only true line starts (no cross-domain predecessors) report, so a
    // 4-node line yields one finding, not one per intermediate node.
    std::unordered_set<uint64_t> reported_sources;
    for (const auto& src : graph.nodes()) {
        if (src.kind != ir::NodeKind::Register)
            continue;
        if (reported_sources.count(src.id))
            continue;

        const clock::ClockDomain* src_dom =
            find_domain_for_node(src.id, domains, register_to_domain);
        if (!src_dom)
            continue;

        // Skip intermediate nodes: if any predecessor is in a different domain,
        // this node is mid-line, not a chain start.
        {
            bool has_cross_pred = false;
            for (uint64_t pred : graph.register_predecessors(src.id, false)) {
                const clock::ClockDomain* pd =
                    find_domain_for_node(pred, domains, register_to_domain);
                if (pd && pd->id != src_dom->id) {
                    has_cross_pred = true;
                    break;
                }
            }
            if (has_cross_pred) {
                reported_sources.insert(src.id);
                continue;
            }
        }

        // DFS along single paths (depth-limited to avoid blowup).
        std::vector<uint64_t> best_chain;
        std::vector<uint64_t> path = {src.id};
        std::unordered_set<uint64_t> on_path = {src.id};
        std::vector<size_t> dom_seq = {src_dom->id};
        std::vector<std::vector<uint64_t>> stack_succs;
        std::vector<size_t> stack_idx;
        stack_succs.push_back(graph.register_successors(src.id));
        stack_idx.push_back(0);

        auto distinct_domains = [&]() {
            std::unordered_set<size_t> s(dom_seq.begin(), dom_seq.end());
            return s.size();
        };

        size_t guard = 0;
        while (!stack_succs.empty() && guard++ < 100000) {
            auto& succs = stack_succs.back();
            size_t& idx = stack_idx.back();
            if (idx >= succs.size()) {
                stack_succs.pop_back();
                stack_idx.pop_back();
                if (!path.empty()) {
                    on_path.erase(path.back());
                    path.pop_back();
                    dom_seq.pop_back();
                }
                continue;
            }
            uint64_t succ = succs[idx++];
            if (on_path.count(succ))
                continue;
            const ir::Node* succ_node = graph.find_node(succ);
            if (!succ_node || succ_node->kind != ir::NodeKind::Register)
                continue;
            const clock::ClockDomain* succ_dom =
                find_domain_for_node(succ, domains, register_to_domain);
            if (!succ_dom)
                continue;
            path.push_back(succ);
            dom_seq.push_back(succ_dom->id);
            on_path.insert(succ);
            if (distinct_domains() >= 3 && path.size() > best_chain.size())
                best_chain = path;
            if (path.size() < 8) {
                stack_succs.push_back(graph.register_successors(succ));
                stack_idx.push_back(0);
            } else {
                path.pop_back();
                dom_seq.pop_back();
                on_path.erase(succ);
            }
        }

        reported_sources.insert(src.id);

        if (best_chain.size() >= 3) {
            Finding dc;
            dc.rule_id = "CDC008";
            dc.rule_name = "multi_domain_daisy_chain";
            dc.severity = "warning";
            dc.source_reg_id = src.id;
            dc.source_reg_name = src.hier_name;
            dc.source_domain = src_dom->name;
            dc.source_loc = src.loc;
            dc.bus_width = src.width;
            dc.safety_status = SafetyStatus::Candidate;
            dc.safety_provenance = "Sequential multi-domain path detected";
            std::string chain_desc;
            for (size_t i = 0; i < best_chain.size(); ++i) {
                const ir::Node* n = graph.find_node(best_chain[i]);
                if (n) {
                    if (!chain_desc.empty())
                        chain_desc += " -> ";
                    chain_desc += n->hier_name;
                }
            }
            dc.reason = "Register '" + src.hier_name + "' is part of a daisy chain crossing " +
                        std::to_string(best_chain.size() - 1) + " clock domains: " + chain_desc +
                        ".";
            findings.push_back(std::move(dc));
        }
    }

    return findings;
}

void CrossingAnalyzer::propagate_uncertainty(ir::Graph& graph,
                                             std::vector<Finding>& findings) const {
    for (const auto& f : findings) {
        if (!f.propagates_uncertainty || f.dest_reg_id == 0)
            continue;
        ir::Node* dest = graph.find_node_mutable(f.dest_reg_id);
        if (!dest)
            continue;
        dest->value_uncertain = true;
        if (dest->uncertainty_source.empty())
            dest->uncertainty_source = f.uncertainty_reason;
    }

    std::vector<uint64_t> work;
    for (const ir::Node& node : graph.nodes()) {
        if (node.value_uncertain)
            work.push_back(node.id);
    }
    size_t hops = 0;
    while (!work.empty() && hops < ir::MAX_PATH_DEPTH) {
        ++hops;
        std::vector<uint64_t> next;
        for (uint64_t id : work) {
            const ir::Node* node = graph.find_node(id);
            if (!node)
                continue;
            auto mark = [&](uint64_t succ) {
                ir::Node* n = graph.find_node_mutable(succ);
                if (!n || n->value_uncertain)
                    return;
                if (n->clock_domain != node->clock_domain)
                    return;
                n->value_uncertain = true;
                n->uncertainty_source = "propagated from '" + node->hier_name + "'";
                next.push_back(succ);
            };
            for (uint64_t succ : graph.successors(id))
                mark(succ);
            for (uint64_t rsucc : graph.register_successors(id, false))
                mark(rsucc);
        }
        work = std::move(next);
    }

    // Emit CDC013 findings for registers that became uncertain due to
    // propagation (not from direct crossing detection).
    for (const ir::Node& node : graph.nodes()) {
        if (!node.value_uncertain || node.kind != ir::NodeKind::Register)
            continue;
        bool already_reported = false;
        for (const auto& f : findings) {
            if ((f.source_reg_id == node.id || f.dest_reg_id == node.id) && f.rule_id != "CDC013") {
                already_reported = true;
                break;
            }
        }
        if (already_reported ||
            node.uncertainty_source.find("propagated from") == std::string::npos)
            continue;

        Finding uf;
        uf.rule_id = "CDC013";
        uf.rule_name = "unknown_propagation";
        uf.severity = "warning";
        uf.source_reg_id = node.id;
        uf.source_reg_name = node.hier_name;
        // The domain as the other findings name it (the resolved root).
        uf.source_domain = node.root_clock.empty() ? node.clock_domain : node.root_clock;
        uf.path.node_ids.push_back(node.id);
        uf.source_loc = node.loc;
        uf.bus_width = node.width;
        uf.propagates_uncertainty = true;
        uf.uncertainty_reason = node.uncertainty_source;
        uf.reason = "Register '" + node.hier_name + "' has uncertain value due to " +
                    node.uncertainty_source + ".";
        uf.safety_status = SafetyStatus::Candidate;
        uf.safety_provenance = "Value uncertainty propagation";
        uf.evidence_chain.push_back(EvidenceStep{"source", "Source: " + node.uncertainty_source,
                                                 "identified", node.loc.file});
        uf.evidence_chain.push_back(
            EvidenceStep{"propagation", "Propagated within domain '" + node.clock_domain + "'",
                         "propagated", node.loc.file});
        findings.push_back(std::move(uf));
    }
}

}  // namespace opencdc::cdc
