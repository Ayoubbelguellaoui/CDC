#ifndef OPENCDC_CDC_SYNCHRONIZER_H
#define OPENCDC_CDC_SYNCHRONIZER_H

#include <string>
#include <vector>

#include "clock/domain.h"
#include "ir/graph.h"

namespace opencdc::cdc {

enum class SyncPattern { None, TwoFF, ThreeFF, FourFF };

struct SynchronizerChain {
    SyncPattern pattern = SyncPattern::None;
    size_t depth = 0;
    std::vector<uint64_t> stage_ids;
    std::vector<std::string> warnings;
    uint64_t source_reg_id = 0;
    std::string clock_domain;
};

class SynchronizerMatcher {
   public:
    std::vector<SynchronizerChain> match(const ir::Graph& graph);

    SyncPattern find_pattern_for_dest(uint64_t dest_reg_id, const ir::Graph& graph,
                                      bool strict = false) const;

    bool has_chain_warnings(const ir::Graph& graph, uint64_t dest_reg_id) const;

   private:
    std::string validate_stage_reset(const ir::Graph& graph, uint64_t stage_id,
                                     uint64_t prev_stage_id) const;

    bool validate_stage_fanout(const ir::Graph& graph, uint64_t stage1_id) const;

    void collect_chain_warnings(const ir::Graph& graph, SynchronizerChain& chain) const;
};

}  // namespace opencdc::cdc

#endif  // OPENCDC_CDC_SYNCHRONIZER_H
