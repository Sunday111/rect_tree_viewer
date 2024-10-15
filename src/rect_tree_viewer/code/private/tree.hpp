#pragma once

#include <optional>
#include <span>
#include <string>
#include <vector>

struct TreeNode
{
    std::string name;
    long double value;
    std::optional<size_t> parent{};
    std::optional<size_t> first_child{};
    std::optional<size_t> next_sibling{};
};

struct TreeHelper
{
    constexpr static void
    GetChildren(std::span<const TreeNode> nodes, size_t node_id, std::vector<size_t>& out_children)
    {
        std::optional<size_t> opt_child_id = nodes[node_id].first_child;
        while (opt_child_id)
        {
            size_t child_id = opt_child_id.value();
            out_children.push_back(child_id);
            opt_child_id = nodes[child_id].next_sibling;
        }
    }
};
