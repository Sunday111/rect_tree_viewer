#include <filesystem>
#include <ranges>

#include "tree.hpp"

[[nodiscard]] std::vector<TreeNode> ReadDirectoryTree(const std::filesystem::path& root_path)
{
    namespace fs = std::filesystem;

    struct WalkEntry
    {
        fs::directory_entry dir_entry;
        size_t id;
    };

    std::vector<TreeNode> nodes;

    // Add the root node
    nodes.push_back({
        .name = root_path.stem().string(),
        .value = 0,
    });

    std::vector<WalkEntry> walk_stack{
        WalkEntry{.dir_entry = fs::directory_entry(root_path), .id = 0},
    };

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
                nodes.push_back({
                    .name = child_dir_entry.path().stem().string(),
                    .value = 0,
                    .parent = walk_entry.id,
                    .next_sibling = nodes[walk_entry.id].first_child,
                });

                nodes[walk_entry.id].first_child = child_id;

                walk_stack.push_back({
                    .dir_entry = child_dir_entry,
                    .id = child_id,
                });
            }
        }
    }

    // Propagate sizes from child to parent
    for (TreeNode& node : nodes | std::views::reverse | std::views::take(nodes.size() - 1))
    {
        [[likely]] if (node.parent)
        {
            nodes[*node.parent].value += node.value;
        }
    }

    return nodes;
}
