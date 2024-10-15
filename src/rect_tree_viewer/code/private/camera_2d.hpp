#pragma once

#include <EverydayTools/Math/FloatRange.hpp>

namespace rect_tree_viewer
{

class Camera
{
public:
    [[nodiscard]] const edt::FloatRange2Df& GetRange() const { return range_; }
    [[nodiscard]] float GetZoom() const { return zoom_; }
    [[nodiscard]] const edt::Vec2f& GetEye() const { return eye_; }

    void SetZoom(float zoom) { zoom_ = zoom; }
    void Pan(const edt::Vec2f& delta);

    void Update(const edt::FloatRange2Df& world_range);

    float zoom_animation_diration_seconds = 0.3f;
    float pan_speed = 0.3f;
    float pan_animation_diration_seconds = 0.3f;

private:
    edt::FloatRange2Df ComputeRange(const edt::FloatRange2Df& world_range) const;

private:
    edt::FloatRange2Df range_ = {};
    float zoom_ = 1.f;
    edt::Vec2f eye_{};
};
}  // namespace rect_tree_viewer
