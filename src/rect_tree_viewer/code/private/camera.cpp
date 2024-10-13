#include "camera.hpp"

namespace rect_tree_viewer
{
edt::FloatRange2Df Camera::ComputeRange(const edt::FloatRange2Df& world_range) const
{
    const auto half_world_extent = world_range.Extent() / 2;
    const auto half_camera_extent = half_world_extent / GetZoom();
    return edt::FloatRange2Df::FromMinMax(GetEye() - half_camera_extent, GetEye() + half_camera_extent);
}

void Camera::Update(const edt::FloatRange2Df& world_range)
{
    range_ = ComputeRange(world_range);
}

void Camera::Zoom(const float delta)
{
    zoom_ += delta;
}

void Camera::Pan(const edt::Vec2f& delta)
{
    eye_ += delta;
}

}  // namespace rect_tree_viewer
