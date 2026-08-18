#ifndef OPENCDC_IR_GRAPH_H
#define OPENCDC_IR_GRAPH_H

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace opencdc::ir {

struct SourceLoc {
    std::string file;
    uint32_t line = 0;
    uint32_t col = 0;
};

enum class NodeKind { Register, Port, Combinational };

enum class ResetPolarity { ActiveHigh, ActiveLow, None };

struct Node {
    uint64_t id;
    std::string hier_name;
    std::string short_name;
    NodeKind kind;
    uint32_t width = 1;
    std::string clock_domain;
    std::string root_clock;
    bool clock_is_gated = false;
    bool clock_is_muxed = false;
    std::string reset_signal;
    ResetPolarity reset_pol = ResetPolarity::None;
    SourceLoc loc;
};

struct Edge {
    uint64_t from_id;
    uint64_t to_id;
};

class Graph {
public:
    Graph() = default;

    uint64_t add_register(const std::string& hier_name,
                          const std::string& clock_domain,
                          uint32_t width,
                          const SourceLoc& loc);

    uint64_t add_port(const std::string& hier_name,
                      uint32_t width,
                      const SourceLoc& loc);

    void add_edge(uint64_t from_id, uint64_t to_id);

    const Node* find_node(uint64_t id) const;
    const Node* find_node_by_name(const std::string& hier_name) const;

    const std::vector<Node>& nodes() const { return nodes_; }
    std::vector<Node>& nodes_mutable() { return nodes_; }
    const std::vector<Edge>& edges() const { return edges_; }

    std::vector<uint64_t> successors(uint64_t id) const;
    std::vector<uint64_t> predecessors(uint64_t id) const;

    size_t register_count() const;
    size_t edge_count() const { return edges_.size(); }

private:
    std::vector<Node> nodes_;
    std::vector<Edge> edges_;
    std::unordered_map<uint64_t, size_t> id_to_idx_;
    std::unordered_map<std::string, size_t> name_to_idx_;
    uint64_t next_id_ = 1;
};

} // namespace opencdc::ir

#endif // OPENCDC_IR_GRAPH_H
