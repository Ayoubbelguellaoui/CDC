#include "cdc/cdc006.h"

namespace opencdc::cdc {

std::vector<Finding> Cdc006Analyzer::analyze(
    const ir::Graph& graph, const std::vector<clock::ClockDomain>& domains,
    const std::unordered_map<uint64_t, size_t>& register_to_domain) {
    std::vector<Finding> findings;

    auto chains = sync_matcher_.match(graph);

    for (const auto& chain : chains) {
        if (chain.stage_ids.empty())
            continue;
        if (chain.pattern == SyncPattern::None)
            continue;

        uint64_t first_stage_id = chain.stage_ids[0];
        const ir::Node* first_stage = graph.find_node(first_stage_id);
        if (!first_stage)
            continue;

        uint64_t second_stage_id = 0;
        for (uint64_t s : graph.register_successors(first_stage_id, true)) {
            const ir::Node* sn = graph.find_node(s);
            if (sn && sn->kind == ir::NodeKind::Register &&
                sn->clock_domain == first_stage->clock_domain) {
                second_stage_id = s;
                break;
            }
        }
        if (!second_stage_id)
            continue;

        int same_domain_preds = 0;
        bool has_comb_pred = false;
        for (uint64_t p : graph.predecessors(second_stage_id)) {
            const ir::Node* pn = graph.find_node(p);
            if (!pn)
                continue;
            if (pn->kind == ir::NodeKind::Register &&
                pn->clock_domain == first_stage->clock_domain) {
                same_domain_preds++;
            } else if (pn->kind == ir::NodeKind::Combinational) {
                has_comb_pred = true;
            }
        }
        if (!has_comb_pred)
            continue;

        for (uint64_t pred_id : graph.register_predecessors(first_stage_id)) {
            const ir::Node* pred = graph.find_node(pred_id);
            if (!pred)
                continue;

            if (pred->kind == ir::NodeKind::Register) {
                if (pred->clock_domain != first_stage->clock_domain) {
                    Finding f;
                    f.rule_id = "CDC006";
                    f.rule_name = "combinational_between_sync";
                    f.severity = "error";
                    f.source_reg_id = pred_id;
                    f.dest_reg_id = first_stage_id;
                    f.source_reg_name = pred->hier_name;
                    f.dest_reg_name = first_stage->hier_name;
                    f.source_domain = pred->clock_domain;
                    f.dest_domain = first_stage->clock_domain;
                    f.path.node_ids = {pred_id, first_stage_id};
                    f.source_loc = pred->loc;
                    f.bus_width = pred->width;

                    std::string chain_type = (chain.pattern == SyncPattern::TwoFF) ? "2FF" : "3FF";
                    f.reason = "Synchronizer chain " + chain_type + " at '" +
                               first_stage->hier_name +
                               "' has combinational logic between stages "
                               "driven by cross-domain source '" +
                               pred->hier_name +
                               "'. "
                               "Combinational logic between synchronization stages defeats the "
                               "purpose of the synchronizer.";

                    findings.push_back(std::move(f));
                    break;
                }
            }
        }
    }

    return findings;
}

}  // namespace opencdc::cdc
