#ifndef OPENCDC_CDC_SYNCHRONIZER_H
#define OPENCDC_CDC_SYNCHRONIZER_H

#include "ir/graph.h"
#include "clock/domain.h"
#include <vector>
#include <string>

namespace opencdc::cdc {

enum class SyncPattern { None, TwoFF, ThreeFF };

struct SynchronizerChain {
    SyncPattern pattern = SyncPattern::None;
    std::vector<uint64_t> stage_ids;
    uint64_t source_reg_id = 0;
    std::string clock_domain;
};

class SynchronizerMatcher {
public:
    std::vector<SynchronizerChain> match(const ir::Graph& graph);

    SyncPattern find_pattern_for_dest(
        uint64_t dest_reg_id,
        const ir::Graph& graph) const;

private:
    bool validate_2ff(const ir::Graph& graph,
                      uint64_t s1, uint64_t s2) const;

    bool validate_3ff(const ir::Graph& graph,
                      uint64_t s1, uint64_t s2, uint64_t s3) const;
};

} // namespace opencdc::cdc

#endif // OPENCDC_CDC_SYNCHRONIZER_H
