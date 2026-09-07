#ifndef OPENCDC_IR_MODULE_TREE_H
#define OPENCDC_IR_MODULE_TREE_H

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "ir/graph.h"

namespace opencdc::ir {

struct ModuleNode {
    std::string name;
    std::string parent_name;
    std::vector<std::string> children;
    std::vector<uint64_t> register_ids;
};

class ModuleTree {
   public:
    void build(const Graph& graph);

    const ModuleNode* find(const std::string& module_name) const;
    std::vector<std::string> ancestors(const std::string& module_name) const;
    std::string common_ancestor(const std::string& a, const std::string& b) const;
    size_t depth(const std::string& module_name) const;
    bool crosses_boundary(uint64_t src_id, uint64_t dst_id, const Graph& graph) const;

    const std::unordered_map<std::string, ModuleNode>& modules() const {
        return modules_;
    }

   private:
    std::unordered_map<std::string, ModuleNode> modules_;
};

}  // namespace opencdc::ir

#endif  // OPENCDC_IR_MODULE_TREE_H
