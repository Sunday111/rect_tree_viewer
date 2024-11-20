#include "rect_tree_draw_data.hpp"

#include <cassert>

namespace rect_tree_viewer
{

std::vector<Rect2d> RectTreeDrawData::Create(const std::span<const TreeNode> nodes, const float padding_factor)
{
    using namespace edt::lazy_matrix_aliases;  // NOLINT

    auto get_node_value = [&](size_t id)
    {
        return nodes[id].value;
    };

    // Make one rectangle for each node. Create root area covering the whole screen (maybe has to be a parameter)
    std::vector<Rect2d> rects(nodes.size());
    rects[0] = {.bottom_left = {-1, -1}, .size = {2, 2}};

    // Region is a set of nodes displayed in one rectanle
    struct Region
    {
        Rect2d rect;
        std::span<const size_t> nodes;
        long double value = 0;
    };

    auto split_rect = [](const Rect2d& rect, const long double split_ratio) -> std::tuple<Rect2d, Rect2d>
    {
        if (rect.size.x() > rect.size.y())
        {
            // split along X
            // left rect
            float left_width = static_cast<float>(static_cast<long double>(rect.size.x()) * split_ratio);
            return std::make_tuple(
                Rect2d{
                    .bottom_left = rect.bottom_left,
                    .size = {left_width, rect.size.y()},
                },
                // right rect
                Rect2d{
                    .bottom_left = {rect.bottom_left.x() + left_width, rect.bottom_left.y()},
                    .size = {rect.size.x() - left_width, rect.size.y()},
                });
        }
        else
        {
            // split along Y
            // bottom rect
            float bottom_height = static_cast<float>(static_cast<long double>(rect.size.y()) * split_ratio);
            return std::make_tuple(
                Rect2d{
                    .bottom_left = rect.bottom_left,
                    .size = {rect.size.x(), bottom_height},
                },
                // top rect
                Rect2d{
                    .bottom_left = {rect.bottom_left.x(), rect.bottom_left.y() + bottom_height},
                    .size = {rect.size.x(), rect.size.y() - bottom_height},
                });
        }
    };

    auto make_inner_rect = [&padding_factor](const Rect2d& rect) -> Rect2d
    {
        const Vec2f rect_size = rect.size * padding_factor;
        return {.bottom_left = rect.bottom_left + (rect.size - rect_size) / 2, .size = rect_size};
    };

    std::vector<size_t> children_nodes;
    std::vector<Region> regions;
    for (const size_t node_id : std::views::iota(size_t{0}, nodes.size()))
    {
        children_nodes.clear();
        TreeHelper::GetChildren(nodes, node_id, children_nodes);

        if (children_nodes.empty())
        {
            continue;
        }

        // sort children by value in descending order
        std::ranges::sort(children_nodes, std::greater{}, get_node_value);

        // Make an inner rectangle for children
        regions.push_back(
            {.rect = make_inner_rect(rects[node_id]), .nodes = children_nodes, .value = get_node_value(node_id)});

        // On each iteration: collect children to get 50+% of value and split the rect along the biggest extent
        while (!regions.empty())
        {
            auto region_to_split = regions.back();
            regions.pop_back();

            if (region_to_split.nodes.size() == 1)
            {
                rects[region_to_split.nodes.front()] = region_to_split.rect;
                continue;
            }

            // Always send the first node to the first region
            size_t first_region_size = 1;
            long double first_region_value = get_node_value(region_to_split.nodes.front());
            while (first_region_value * 2.02L < region_to_split.value)
            {
                size_t child_id = region_to_split.nodes[first_region_size];
                first_region_value += nodes[child_id].value;
                first_region_size++;
            }

            const long double split_ratio = first_region_value / region_to_split.value;
            auto [first_rect, second_rect] = split_rect(region_to_split.rect, split_ratio);

            regions.push_back({
                .rect = first_rect,
                .nodes = region_to_split.nodes.subspan(0, first_region_size),
                .value = first_region_value,
            });
            assert(!regions.back().nodes.empty());

            regions.push_back({
                .rect = second_rect,
                .nodes = region_to_split.nodes.subspan(first_region_size),
                .value = region_to_split.value - first_region_value,
            });
            assert(!regions.back().nodes.empty());
        }
    }

    return rects;
}
}  // namespace rect_tree_viewer
