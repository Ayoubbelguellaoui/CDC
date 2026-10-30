#include "cdc/synchronizer.h"

#include <algorithm>
#include <unordered_set>
#include <vector>

namespace opencdc::cdc {

std::string SynchronizerMatcher::validate_stage_reset(const ir::Graph& graph, uint64_t stage_id,
                                                      uint64_t prev_stage_id) const {
    const ir::Node* stage = graph.find_node(stage_id);
    const ir::Node* prev = graph.find_node(prev_stage_id);
    if (!stage || !prev)
        return {};

    // Asynchronous reset on a metastability-sensitive stage is a hazard
    if (stage->is_async_reset && !prev->is_async_reset)
        return "Asynchronous reset on stage '" + stage->hier_name +
               "' may cause metastability on deassert";

    // Both stages have async reset but different signals — inconsistent reset domains
    if (stage->is_async_reset && prev->is_async_reset && !stage->reset_signal.empty() &&
        !prev->reset_signal.empty() && stage->reset_signal != prev->reset_signal)
        return "Different async reset signals between synchronizer stages";

    // Synchronous reset after async reset — inconsistent reset strategy
    if (!stage->is_async_reset && prev->is_async_reset && !stage->reset_signal.empty() &&
        !prev->reset_signal.empty())
        return "Synchronous reset on stage '" + stage->hier_name + "' after async reset on '" +
               prev->hier_name + "'";

    // Reset polarity mismatch between stages
    if (!stage->reset_signal.empty() && !prev->reset_signal.empty() &&
        stage->reset_pol != prev->reset_pol)
        return "Reset polarity mismatch between synchronizer stages";

    return {};
}

bool SynchronizerMatcher::validate_stage_fanout(const ir::Graph& graph, uint64_t stage1_id,
                                                bool first) const {
    const ir::Node* stage1 = graph.find_node(stage1_id);
    if (!stage1)
        return true;

    // A later stage: a second same-domain register it feeds as data shortens
    // the chain the designer chose. Its value used as a condition (a
    // handshake ack, a data-enable qualifier) is the synchronized output, and
    // a register copying it may be no stage at all.
    if (!first) {
        size_t same_domain = 0;
        for (uint64_t succ : graph.register_successors(stage1_id, false, true)) {
            const ir::Node* n = graph.find_node(succ);
            if (n && n->clock_domain == stage1->clock_domain && ++same_domain > 1)
                return false;
        }
        return true;
    }

    // The first stage may still be metastable: it must feed the second stage
    // only. Any other register it reaches - through nets, ports or logic, as
    // data or condition - or a top-level output port reads the unresolved
    // value.
    size_t registers = 0;
    std::unordered_set<uint64_t> visited{stage1_id};
    std::vector<uint64_t> stack = graph.successors(stage1_id);
    while (!stack.empty()) {
        uint64_t cur = stack.back();
        stack.pop_back();
        if (!visited.insert(cur).second)
            continue;
        const ir::Node* n = graph.find_node(cur);
        if (!n)
            continue;
        if (n->kind == ir::NodeKind::Register) {
            if (++registers > 1)
                return false;
            continue;
        }
        if (n->kind == ir::NodeKind::Port &&
            std::count(n->hier_name.begin(), n->hier_name.end(), '.') == 1)
            return false;  // a top-level output
        if (n->kind != ir::NodeKind::Net && n->kind != ir::NodeKind::Port &&
            n->kind != ir::NodeKind::Combinational)
            continue;
        for (uint64_t succ : graph.successors(cur))
            stack.push_back(succ);
    }
    return true;
}

void SynchronizerMatcher::collect_chain_warnings(const ir::Graph& graph,
                                                 SynchronizerChain& chain) const {
    if (chain.stage_ids.size() < 2)
        return;

    // Check reset validation for each consecutive stage pair
    for (size_t i = 0; i + 1 < chain.stage_ids.size(); ++i) {
        std::string warning =
            validate_stage_reset(graph, chain.stage_ids[i + 1], chain.stage_ids[i]);
        if (!warning.empty())
            chain.warnings.push_back(std::move(warning));
    }

    // Check fanout from every intermediate stage (not just stage 1)
    for (size_t i = 0; i + 1 < chain.stage_ids.size(); ++i) {
        if (!validate_stage_fanout(graph, chain.stage_ids[i], i == 0)) {
            chain.warnings.push_back("Stage " + std::to_string(i + 1) +
                                     " fanout: multiple same-domain successors detected");
        }
    }
}

// A stage of a synchronizer chain is one bit wide - each bit of a bus
// synchronized on its own may resolve in a different cycle - unless the
// source is gray-coded (one bit changes per cycle): then a stage as wide as
// the destination is one.
static bool stage_width_ok(const ir::Node& stage, const ir::Node& dest, bool gray_source) {
    return stage.width == 1 || (gray_source && stage.width == dest.width);
}

// Maps a chain length in stages to the reported pattern (shared by the
// node-walk and the packed-vector path).
static SyncPattern pattern_from_depth(size_t depth) {
    if (depth == 2)
        return SyncPattern::TwoFF;
    if (depth == 3)
        return SyncPattern::ThreeFF;
    if (depth == 4)
        return SyncPattern::FourFF;
    if (depth >= 5)
        return SyncPattern::NStage;  // 5+ stages
    return SyncPattern::None;        // no second stage: not a chain
}

SyncPattern SynchronizerMatcher::find_pattern_for_dest(uint64_t dest_reg_id, const ir::Graph& graph,
                                                       bool strict, bool gray_source) const {
    const ir::Node* dest = graph.find_node(dest_reg_id);
    if (!dest)
        return SyncPattern::None;
    if (dest->clock_domain.empty())
        return SyncPattern::None;
    // Multi-bit destinations must be frontend-verified packed shift chains
    // (e.g. `sync <= {sync[0], src}`); a bare wide node with a cross-domain
    // predecessor — or a self-loop from `sync <= sync | src` — is not one.
    if (dest->width != 1 && dest->packed_shift_depth < 2 && !gray_source)
        return SyncPattern::None;

    bool has_cross_domain_register_pred = false;
    for (uint64_t pred_id : graph.register_predecessors(dest_reg_id, false, true)) {
        const ir::Node* pred = graph.find_node(pred_id);
        if (pred && pred->kind == ir::NodeKind::Register &&
            pred->clock_domain != dest->clock_domain) {
            has_cross_domain_register_pred = true;
            break;
        }
    }
    if (!has_cross_domain_register_pred)
        return SyncPattern::None;

    if (strict) {
        for (uint64_t pred_id : graph.register_predecessors(dest_reg_id, false, true)) {
            if (pred_id == dest_reg_id)
                continue;
            const ir::Node* pred = graph.find_node(pred_id);
            if (pred && pred->kind == ir::NodeKind::Register &&
                pred->clock_domain == dest->clock_domain) {
                return SyncPattern::None;
            }
        }
    }

    // Packed shift chain: the RTL frontend verified a pure intra-vector
    // chain, so one multi-bit node stands for the whole chain — depth comes
    // from the tag, no node walk (the only intra-node edge is the self-loop).
    if (dest->width != 1 && dest->packed_shift_depth >= 2)
        return pattern_from_depth(dest->packed_shift_depth);

    // Count actual depth by walking the chain
    size_t depth = 1;  // dest itself counts as stage 1
    uint64_t current = dest_reg_id;

    for (int stage = 0; stage < 10; ++stage) {
        uint64_t next_id = 0;
        for (uint64_t succ : graph.register_successors(current, false, true)) {
            const ir::Node* n = graph.find_node(succ);
            if (!n || n->kind != ir::NodeKind::Register)
                continue;
            if (n->clock_domain != dest->clock_domain)
                continue;
            if (!stage_width_ok(*n, *dest, gray_source))
                continue;
            // Reconvergent consumer guard: a sync stage must have exactly one
            // same-domain predecessor. Multiple same-domain preds means this is
            // a consumer fed by independent paths, not a chain stage.
            {
                size_t same_domain_preds = 0;
                for (uint64_t pred : graph.register_predecessors(succ, false, true)) {
                    const ir::Node* pn = graph.find_node(pred);
                    if (pn && pn->kind == ir::NodeKind::Register &&
                        pn->clock_domain == n->clock_domain) {
                        if (++same_domain_preds > 1)
                            break;
                    }
                }
                if (same_domain_preds > 1)
                    continue;
            }
            if (strict && current != dest_reg_id) {
                bool has_unexpected_pred = false;
                for (uint64_t pred : graph.register_predecessors(succ, false, true)) {
                    const ir::Node* pn = graph.find_node(pred);
                    if (!pn || pn->kind != ir::NodeKind::Register)
                        continue;
                    if (pred == current || pred == succ)
                        continue;
                    if (pn->clock_domain == n->clock_domain) {
                        has_unexpected_pred = true;
                        break;
                    }
                }
                if (has_unexpected_pred)
                    continue;
            }
            next_id = succ;
            break;
        }
        if (!next_id)
            break;
        current = next_id;
        depth++;
    }

    return pattern_from_depth(depth);
}

bool SynchronizerMatcher::has_chain_warnings(const ir::Graph& graph, uint64_t dest_reg_id,
                                             bool strict, bool gray_source) const {
    const ir::Node* dest = graph.find_node(dest_reg_id);
    if (!dest)
        return false;
    // A packed chain read in its first stage: the frontend saw the bits.
    if (dest->packed_shift_depth >= 2 && dest->packed_early_tap)
        return true;

    // Check reset validation for stage pairs in the chain
    uint64_t current = dest_reg_id;
    for (int stage = 0; stage < 10; ++stage) {
        uint64_t next_id = 0;
        for (uint64_t succ : graph.register_successors(current, false, true)) {
            const ir::Node* n = graph.find_node(succ);
            if (!n || n->kind != ir::NodeKind::Register)
                continue;
            if (n->clock_domain != dest->clock_domain)
                continue;
            if (!stage_width_ok(*n, *dest, gray_source))
                continue;
            // Strict check: reject if successor has an unexpected same-domain predecessor
            if (strict && current != dest_reg_id) {
                bool has_unexpected_pred = false;
                for (uint64_t pred : graph.register_predecessors(succ, false, true)) {
                    const ir::Node* pn = graph.find_node(pred);
                    if (!pn || pn->kind != ir::NodeKind::Register)
                        continue;
                    if (pred == current || pred == succ)
                        continue;
                    if (pn->clock_domain == n->clock_domain) {
                        has_unexpected_pred = true;
                        break;
                    }
                }
                if (has_unexpected_pred)
                    continue;
            }
            next_id = succ;
            break;
        }
        if (!next_id)
            break;

        // Check reset between current and next
        std::string warning = validate_stage_reset(graph, next_id, current);
        if (!warning.empty())
            return true;

        // Check fanout from every intermediate stage
        if (!validate_stage_fanout(graph, current, current == dest_reg_id))
            return true;

        current = next_id;
    }
    return false;
}

std::vector<SynchronizerChain> SynchronizerMatcher::match(const ir::Graph& graph) {
    std::vector<SynchronizerChain> chains;

    for (const auto& node : graph.nodes()) {
        if (node.kind != ir::NodeKind::Register)
            continue;

        for (uint64_t pred_id : graph.register_predecessors(node.id, false, true)) {
            const ir::Node* pred = graph.find_node(pred_id);
            if (!pred || pred->kind != ir::NodeKind::Register)
                continue;
            if (pred->clock_domain == node.clock_domain)
                continue;

            SyncPattern pat = find_pattern_for_dest(node.id, graph, true);
            if (pat == SyncPattern::None)
                continue;

            bool exists = false;
            for (const auto& c : chains) {
                if (c.source_reg_id == pred_id && c.clock_domain == node.clock_domain &&
                    !c.stage_ids.empty() && c.stage_ids[0] == node.id) {
                    exists = true;
                    break;
                }
            }
            if (exists)
                continue;

            SynchronizerChain chain;
            chain.pattern = pat;
            chain.source_reg_id = pred_id;
            chain.clock_domain = node.clock_domain;
            chain.stage_ids.push_back(node.id);

            uint64_t current = node.id;
            for (int stage = 0; stage < 10; ++stage) {
                uint64_t next_id = 0;
                for (uint64_t succ : graph.register_successors(current, false, true)) {
                    const ir::Node* n = graph.find_node(succ);
                    if (!n || n->kind != ir::NodeKind::Register)
                        continue;
                    if (n->clock_domain != node.clock_domain)
                        continue;
                    if (n->width != 1)
                        continue;
                    // Strict check: reject if successor has an unexpected same-domain predecessor
                    if (current != node.id) {
                        bool has_unexpected_pred = false;
                        for (uint64_t pred : graph.register_predecessors(succ, false, true)) {
                            const ir::Node* pn = graph.find_node(pred);
                            if (!pn || pn->kind != ir::NodeKind::Register)
                                continue;
                            if (pred == current || pred == succ)
                                continue;
                            if (pn->clock_domain == n->clock_domain) {
                                has_unexpected_pred = true;
                                break;
                            }
                        }
                        if (has_unexpected_pred)
                            continue;
                    }
                    next_id = succ;
                    break;
                }
                if (!next_id)
                    break;
                chain.stage_ids.push_back(next_id);
                current = next_id;
            }

            collect_chain_warnings(graph, chain);
            chain.depth = chain.stage_ids.size();
            // A packed chain has a single physical stage node standing for
            // all of its vector-internal stages.
            if (node.width != 1)
                chain.depth = node.packed_shift_depth;
            chains.push_back(std::move(chain));
        }
    }

    return chains;
}

size_t SynchronizerMatcher::chain_depth(uint64_t dest_reg_id, const ir::Graph& graph,
                                        bool gray_source) const {
    const ir::Node* dest = graph.find_node(dest_reg_id);
    if (!dest || dest->clock_domain.empty())
        return 0;
    if (dest->width != 1 && dest->packed_shift_depth < 2 && !gray_source)
        return 0;

    bool has_cross_domain_pred = false;
    for (uint64_t pred_id : graph.register_predecessors(dest_reg_id, false, true)) {
        const ir::Node* pred = graph.find_node(pred_id);
        if (pred && pred->kind == ir::NodeKind::Register &&
            pred->clock_domain != dest->clock_domain) {
            has_cross_domain_pred = true;
            break;
        }
    }
    if (!has_cross_domain_pred)
        return 0;

    // Packed chain: all stages live inside this node (see find_pattern).
    if (dest->width != 1 && dest->packed_shift_depth >= 2)
        return dest->packed_shift_depth;

    size_t depth = 1;
    uint64_t current = dest_reg_id;
    for (int stage = 0; stage < 10; ++stage) {
        uint64_t next_id = 0;
        for (uint64_t succ : graph.register_successors(current, false, true)) {
            const ir::Node* n = graph.find_node(succ);
            if (!n || n->kind != ir::NodeKind::Register)
                continue;
            if (n->clock_domain != dest->clock_domain)
                continue;
            if (!stage_width_ok(*n, *dest, gray_source))
                continue;
            next_id = succ;
            break;
        }
        if (!next_id)
            break;
        current = next_id;
        depth++;
    }
    return depth;
}

bool SynchronizerMatcher::below_min_stages(uint64_t dest_reg_id, const ir::Graph& graph,
                                           int min_stages, bool gray_source) const {
    if (min_stages <= 1)
        return false;
    size_t depth = chain_depth(dest_reg_id, graph, gray_source);
    return depth > 0 && static_cast<int>(depth) < min_stages;
}

}  // namespace opencdc::cdc
