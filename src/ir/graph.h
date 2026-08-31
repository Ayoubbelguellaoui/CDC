#ifndef OPENCDC_IR_GRAPH_H
#define OPENCDC_IR_GRAPH_H

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace opencdc::ir {

struct SourceLoc {
    std::string file;
    uint32_t line = 0;
    uint32_t col = 0;
};

enum class NodeKind { Register, Port, Net, Combinational };

enum class ResetPolarity { ActiveHigh, ActiveLow, None };

enum class LogicType {
    None,
    And,
    Or,
    Xor,
    Not,
    Mux,
    Concat,
    GrayEncoder,
    GrayDecoder,
    HandshakeValid,
    HandshakeReady,
    AsyncFifoPtr,
    Unknown
};

struct Node {
    uint64_t id;
    std::string hier_name;
    std::string short_name;
    std::string module_path;
    NodeKind kind;
    uint32_t width = 1;
    std::string clock_domain;
    std::string root_clock;
    bool clock_is_gated = false;
    bool clock_is_muxed = false;
    std::string reset_signal;
    ResetPolarity reset_pol = ResetPolarity::None;
    bool is_async_reset = false;
    SourceLoc loc;

    // Combinational logic tracking
    LogicType logic_type = LogicType::None;
    std::vector<uint64_t> logic_inputs;
    std::string logic_expression;

    // Pattern recognition flags
    bool is_gray_coded = false;
    bool is_handshake_signal = false;
    bool is_async_fifo_ptr = false;
};

struct Edge {
    uint64_t from_id;
    uint64_t to_id;
};

struct ValidationResult {
    std::vector<std::string> errors;
    bool ok() const { return errors.empty(); }
};

// E1-E3: Safety limits to prevent OOM on pathological designs
static constexpr size_t MAX_GRAPH_NODES = 500000;
static constexpr size_t MAX_GRAPH_EDGES = 1000000;
static constexpr size_t MAX_PATH_DEPTH = 50;

class Graph {
public:
    Graph() = default;

    uint64_t add_register(const std::string& hier_name,
                          const std::string& clock_domain,
                          uint32_t width,
                          const SourceLoc& loc,
                          const std::string& module_path = "");

    uint64_t add_port(const std::string& hier_name,
                      uint32_t width,
                      const SourceLoc& loc,
                      const std::string& module_path = "");

    uint64_t add_net(const std::string& hier_name,
                     uint32_t width,
                     const SourceLoc& loc,
                     const std::string& module_path = "");

    uint64_t add_combinational(const std::string& hier_name,
                               LogicType logic_type,
                               const std::vector<uint64_t>& inputs,
                               uint32_t width,
                               const SourceLoc& loc,
                               const std::string& module_path = "");

    void add_edge(uint64_t from_id, uint64_t to_id);

    const Node* find_node(uint64_t id) const;
    Node* find_node_mutable(uint64_t id);
    const Node* find_node_by_name(const std::string& hier_name) const;

    const std::vector<Node>& nodes() const { return nodes_; }
    std::vector<Node>& nodes_mutable() { return nodes_; }
    const std::vector<Edge>& edges() const { return edges_; }

    std::vector<uint64_t> successors(uint64_t id) const;
    std::vector<uint64_t> predecessors(uint64_t id) const;

    std::vector<uint64_t> register_predecessors(
        uint64_t id, bool traverse_combinational = true) const;
    std::vector<uint64_t> register_successors(
        uint64_t id, bool traverse_combinational = true) const;

    struct RegPath {
        uint64_t src_reg_id;
        uint64_t dst_reg_id;
        std::vector<uint64_t> node_ids;
        bool has_combinational;
    };

    struct PathTraversalResult {
        std::vector<RegPath> paths;
        bool truncated = false;
        size_t max_paths = 0;
        size_t visited_nodes = 0;
        size_t visited_edges = 0;
    };

    PathTraversalResult find_register_paths(
        uint64_t src_reg_id, size_t max_paths = 10000) const;

    ValidationResult validate() const;
    bool truncated() const { return truncated_; }

    bool is_data_node(NodeKind k) const {
        return k == NodeKind::Port || k == NodeKind::Net ||
               k == NodeKind::Combinational;
    }

    size_t register_count() const;
    size_t edge_count() const { return edges_.size(); }
    uint64_t generation() const { return generation_; }

private:
    std::vector<Node> nodes_;
    std::vector<Edge> edges_;
    std::unordered_map<uint64_t, size_t> id_to_idx_;
    std::unordered_map<std::string, size_t> name_to_idx_;
    std::unordered_map<uint64_t, std::vector<uint64_t>> adj_;
    std::unordered_map<uint64_t, std::vector<uint64_t>> radj_;
    std::unordered_set<uint64_t> edge_set_;
    uint64_t next_id_ = 1;
    uint64_t generation_ = 0;
    bool truncated_ = false;
};

} // namespace opencdc::ir

#endif // OPENCDC_IR_GRAPH_H
