#include "ir/graph.h"
#include <algorithm>

namespace opencdc::ir {

uint64_t Graph::add_register(const std::string& hier_name,
                             const std::string& clock_domain,
                             uint32_t width,
                             const SourceLoc& loc) {
    uint64_t id = next_id_++;
    size_t idx = nodes_.size();
    nodes_.push_back({id, hier_name, hier_name, NodeKind::Register, width,
                      clock_domain, "", false, false, "", ResetPolarity::None, loc});
    id_to_idx_[id] = idx;
    name_to_idx_[hier_name] = idx;
    return id;
}

uint64_t Graph::add_port(const std::string& hier_name,
                         uint32_t width,
                         const SourceLoc& loc) {
    uint64_t id = next_id_++;
    size_t idx = nodes_.size();
    nodes_.push_back({id, hier_name, hier_name, NodeKind::Port, width,
                      "", "", false, false, "", ResetPolarity::None, loc});
    id_to_idx_[id] = idx;
    name_to_idx_[hier_name] = idx;
    return id;
}

void Graph::add_edge(uint64_t from_id, uint64_t to_id) {
    edges_.push_back({from_id, to_id});
}

const Node* Graph::find_node(uint64_t id) const {
    auto it = id_to_idx_.find(id);
    if (it == id_to_idx_.end()) return nullptr;
    return &nodes_[it->second];
}

const Node* Graph::find_node_by_name(const std::string& hier_name) const {
    auto it = name_to_idx_.find(hier_name);
    if (it == name_to_idx_.end()) return nullptr;
    return &nodes_[it->second];
}

std::vector<uint64_t> Graph::successors(uint64_t id) const {
    std::vector<uint64_t> result;
    for (const auto& e : edges_) {
        if (e.from_id == id) result.push_back(e.to_id);
    }
    return result;
}

std::vector<uint64_t> Graph::predecessors(uint64_t id) const {
    std::vector<uint64_t> result;
    for (const auto& e : edges_) {
        if (e.to_id == id) result.push_back(e.from_id);
    }
    return result;
}

size_t Graph::register_count() const {
    return std::count_if(nodes_.begin(), nodes_.end(),
                         [](const Node& n) { return n.kind == NodeKind::Register; });
}

} // namespace opencdc::ir
