#pragma once

#include <vector>

#include "EverydayTools/Math/Matrix.hpp"
#include "klgl/rendering/painter2d.hpp"
#include "tree.hpp"


namespace rect_tree_viewer
{

struct Rect2d
{
    edt::Vec2f bottom_left{};
    edt::Vec2f size{};

    [[nodiscard]] constexpr klgl::Painter2d::Rect2d ToPainterRect(edt::Vec4u8 color) const
    {
        return {
            .center = bottom_left + size / 2,
            .size = size,
            .color = color,
            .rotation_degrees = 0.f,
        };
    }

    [[nodiscard]] constexpr bool Contains(const edt::Vec2f& position) const
    {
        auto d = position - bottom_left;

        return d.x() >= 0 && d.x() <= size.x() && d.y() >= 0 && d.y() <= size.y();
    }
};

class RectTreeDrawData
{
public:
    [[nodiscard]] static std::vector<Rect2d> Create(
        const std::span<const TreeNode> nodes,
        const float padding_factor = 0.97f);
};
}  // namespace rect_tree_viewer
