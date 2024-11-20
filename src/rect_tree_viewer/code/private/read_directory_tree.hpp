#pragma once

#include <fmt/std.h>

#include <filesystem>
#include <ranges>
#include <unordered_map>

#include "path_helpers.hpp"
#include "tree.hpp"

struct ReadDirTreeEntry
{
    std::filesystem::directory_entry dir_entry;
    size_t id;
};

inline std::vector<TreeNode> ReadDirectoryTreeMulti(
    std::optional<std::string_view> root_node_name,
    std::span<const std::filesystem::path> paths,
    std::unordered_map<size_t, size_t>* out_root_node_id_to_path_index)
{
    namespace fs = std::filesystem;
    std::vector<TreeNode> nodes;
    std::vector<ReadDirTreeEntry> walk_stack;

    std::optional<size_t> common_root_id;
    if (root_node_name)
    {
        // Add the root node
        common_root_id = nodes.size();
        nodes.push_back({
            .name = std::string{*root_node_name},
            .value = 0,
        });
    }

    // Add root paths
    for (const size_t i : std::views::iota(size_t{0}, paths.size()))
    {
        const auto& path = paths[i];
        size_t node_id = nodes.size();
        nodes.push_back({
            .name = path.stem().string(),
            .value = 0,
            .parent = common_root_id,
        });

        walk_stack.push_back({
            .dir_entry = fs::directory_entry(path),
            .id = node_id,
        });

        if (common_root_id)
        {
            nodes[node_id].next_sibling = nodes.front().first_child;
            nodes.front().first_child = node_id;
        }

        if (out_root_node_id_to_path_index)
        {
            (*out_root_node_id_to_path_index)[node_id] = i;
        }
    }

    while (!walk_stack.empty())
    {
        auto walk_entry = std::move(walk_stack.back());
        walk_stack.pop_back();

        if (fs::is_regular_file(walk_entry.dir_entry))
        {
            TreeNode& node = nodes[walk_entry.id];
            node.name = walk_entry.dir_entry.path().filename().string();
            node.value = static_cast<long double>(fs::file_size(walk_entry.dir_entry.path()));
        }
        else if (fs::is_directory(walk_entry.dir_entry))
        {
            for (const auto& child_dir_entry : fs::directory_iterator(walk_entry.dir_entry))
            {
                size_t child_id = nodes.size();
                long double value = 0;
                std::error_code err;
                const bool is_file = fs::is_regular_file(child_dir_entry.path(), err);
                if (is_file)
                {
                    value = static_cast<long double>(fs::file_size(child_dir_entry.path()));
                }
                else if (err)
                {
                    continue;
                }
                else
                {
                    try
                    {
                        fs::directory_iterator iterator(child_dir_entry.path());
                    }
                    catch (const std::exception&)
                    {
                        continue;
                    };

                    walk_stack.push_back({
                        .dir_entry = child_dir_entry,
                        .id = child_id,
                    });
                }

                nodes.push_back({
                    .name = PathHelpers::PathToUTF8(child_dir_entry.path().stem()),
                    .value = value,
                    .parent = walk_entry.id,
                    .next_sibling = nodes[walk_entry.id].first_child,
                });

                nodes[walk_entry.id].first_child = child_id;
            }
        }
    }

    // Propagate sizes from child to parent
    for (TreeNode& node : nodes | std::views::reverse)
    {
        [[likely]] if (node.parent)
        {
            nodes[*node.parent].value += node.value;
        }
    }

    return nodes;
}

inline std::vector<TreeNode> ReadDirectoryTree(const std::filesystem::path& root_path)
{
    return ReadDirectoryTreeMulti(std::nullopt, std::span{&root_path, 1}, nullptr);
}
