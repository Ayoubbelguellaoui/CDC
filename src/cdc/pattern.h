#ifndef OPENCDC_CDC_PATTERN_H
#define OPENCDC_CDC_PATTERN_H

#include "ir/graph.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <optional>

namespace opencdc::cdc {

struct AsyncFifoPattern {
    uint64_t read_ptr_id;
    uint64_t write_ptr_id;
    uint64_t data_bus_id;
    std::string read_domain;
    std::string write_domain;
    bool has_gray_encoding;
    bool verified;
};

struct HandshakePattern {
    uint64_t valid_id;
    uint64_t ready_id;
    uint64_t data_id;
    std::string source_domain;
    std::string dest_domain;
    bool verified;
};

struct GrayCodePattern {
    uint64_t encoder_id;
    uint64_t decoder_id;
    std::vector<uint64_t> data_path;
    bool verified;
};

class PatternRecognizer {
public:
    PatternRecognizer() = default;

    std::vector<AsyncFifoPattern> detect_async_fifos(const ir::Graph& graph) const;
    std::vector<HandshakePattern> detect_handshakes(const ir::Graph& graph) const;
    std::vector<GrayCodePattern> detect_gray_encoding(const ir::Graph& graph) const;

    bool is_gray_coded(uint64_t node_id, const ir::Graph& graph) const;
    bool is_handshake_signal(uint64_t node_id, const ir::Graph& graph) const;
    bool is_async_fifo_ptr(uint64_t node_id, const ir::Graph& graph) const;
    bool is_verified_safe_crossing(uint64_t src_id, uint64_t dst_id,
                                   const ir::Graph& graph) const;

    void analyze_and_annotate(ir::Graph& graph);

    // Pre-compute pattern caches on the main thread before parallel analysis.
    void ensure_patterns(const ir::Graph& graph) const;

private:
    bool detect_gray_encoder(const ir::Node& node, const ir::Graph& graph,
                            std::vector<uint64_t>& inputs) const;
    bool detect_gray_decoder(const ir::Node& node, const ir::Graph& graph,
                            std::vector<uint64_t>& inputs) const;
    bool detect_xor_pattern(const ir::Node& node, const ir::Graph& graph) const;
    bool verify_gray_encoder_structure(uint64_t node_id, const ir::Graph& graph) const;

    bool detect_valid_ready_pair(uint64_t valid_id, uint64_t ready_id,
                                const ir::Graph& graph) const;

    std::string extract_base_name(const std::string& hier_name) const;
    std::string extract_module_name(const std::string& hier_name) const;

    // Pattern results are computed once per graph and reused by
    // is_verified_safe_crossing, instead of re-running the detectors (up to
    // O(N^2)) on every crossing query.
    mutable std::vector<AsyncFifoPattern> fifo_cache_;
    mutable std::vector<HandshakePattern> handshake_cache_;
    mutable std::vector<GrayCodePattern> gray_cache_;
    mutable uint64_t cached_graph_generation_ = 0;
};

} // namespace opencdc::cdc

#endif // OPENCDC_CDC_PATTERN_H
