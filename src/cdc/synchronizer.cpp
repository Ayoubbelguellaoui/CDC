#include "cdc/synchronizer.h"

namespace opencdc::cdc {

std::string SynchronizerMatcher::validate_stage_reset(const ir::Graph& graph,
                                                      uint64_t stage_id,
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
        return "Synchronous reset on stage '" + stage->hier_name +
               "' after async reset on '" + prev->hier_name + "'";

    // Reset polarity mismatch between stages
    if (!stage->reset_signal.empty() && !prev->reset_signal.empty() &&
        stage->reset_pol != prev->reset_pol)
        return "Reset polarity mismatch between synchronizer stages";

    return {};
}

bool SynchronizerMatcher::validate_stage_fanout(const ir::Graph& graph,
                                                  uint64_t stage1_id) const {
    const ir::Node* stage1 = graph.find_node(stage1_id);
    if (!stage1)
        return true;

    size_t same_domain_successors = 0;
    for (uint64_t succ : graph.register_successors(stage1_id, false)) {
        const ir::Node* n = graph.find_node(succ);
        if (n && n->kind == ir::NodeKind::Register && n->clock_domain == stage1->clock_domain) {
            same_domain_successors++;
            if (same_domain_successors > 1)
                return false;
        }
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
        if (!validate_stage_fanout(graph, chain.stage_ids[i])) {
            chain.warnings.push_back("Stage " + std::to_string(i + 1) +
                                     " fanout: multiple same-domain successors detected");
        }
    }
}

SyncPattern SynchronizerMatcher::find_pattern_for_dest(uint64_t dest_reg_id, const ir::Graph& graph,
                                                       bool strict) const {
    const ir::Node* dest = graph.find_node(dest_reg_id);
    if (!dest)
        return SyncPattern::None;
    if (dest->clock_domain.empty())
        return SyncPattern::None;
    if (dest->width != 1)
        return SyncPattern::None;

    bool has_cross_domain_register_pred = false;
    for (uint64_t pred_id : graph.register_predecessors(dest_reg_id, false)) {
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
        for (uint64_t pred_id : graph.register_predecessors(dest_reg_id, false)) {
            if (pred_id == dest_reg_id)
                continue;
            const ir::Node* pred = graph.find_node(pred_id);
            if (pred && pred->kind == ir::NodeKind::Register &&
                pred->clock_domain == dest->clock_domain) {
                return SyncPattern::None;
            }
        }
    }

    // Count actual depth by walking the chain
    size_t depth = 1;  // dest itself counts as stage 1
    uint64_t current = dest_reg_id;

    for (int stage = 0; stage < 10; ++stage) {
        uint64_t next_id = 0;
        for (uint64_t succ : graph.register_successors(current, false)) {
            const ir::Node* n = graph.find_node(succ);
            if (!n || n->kind != ir::NodeKind::Register)
                continue;
            if (n->clock_domain != dest->clock_domain)
                continue;
            if (n->width != 1)
                continue;
            if (strict && current != dest_reg_id) {
                bool has_unexpected_pred = false;
                for (uint64_t pred : graph.register_predecessors(succ, false)) {
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

    if (depth == 1)
        return SyncPattern::None;
    if (depth == 2)
        return SyncPattern::TwoFF;
    if (depth == 3)
        return SyncPattern::ThreeFF;
    if (depth == 4)
        return SyncPattern::FourFF;
    return SyncPattern::ThreeFF;  // 5+ stages treated as ThreeFF+
}

bool SynchronizerMatcher::has_chain_warnings(const ir::Graph& graph,
                                              uint64_t dest_reg_id) const {
    const ir::Node* dest = graph.find_node(dest_reg_id);
    if (!dest)
        return false;

    // Check reset validation for stage pairs in the chain
    uint64_t current = dest_reg_id;
    for (int stage = 0; stage < 10; ++stage) {
        uint64_t next_id = 0;
        for (uint64_t succ : graph.register_successors(current, false)) {
            const ir::Node* n = graph.find_node(succ);
            if (!n || n->kind != ir::NodeKind::Register)
                continue;
            if (n->clock_domain != dest->clock_domain)
                continue;
            if (n->width != 1)
                continue;
            // Strict check: reject if successor has an unexpected same-domain predecessor
            if (current != dest_reg_id) {
                bool has_unexpected_pred = false;
                for (uint64_t pred : graph.register_predecessors(succ, false)) {
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
        if (!validate_stage_fanout(graph, current))
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

        for (uint64_t pred_id : graph.register_predecessors(node.id, false)) {
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
                for (uint64_t succ : graph.register_successors(current, false)) {
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
                        for (uint64_t pred : graph.register_predecessors(succ, false)) {
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
            chains.push_back(std::move(chain));
        }
    }

    return chains;
}

}  // namespace opencdc::cdc
