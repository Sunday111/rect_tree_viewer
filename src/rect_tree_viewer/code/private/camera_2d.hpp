#pragma once

#include <EverydayTools/Math/FloatRange.hpp>
#include <EverydayTools/Math/Math.hpp>

namespace rect_tree_viewer
{

class WorldTransforms2d
{
public:
    edt::Mat3f world_to_camera;
    edt::Mat3f world_to_view;
    edt::Mat3f screen_to_world;
};

class Camera2d
{
public:
    [[nodiscard]] constexpr float GetZoom() const { return zoom_; }
    constexpr void SetZoom(float zoom) { zoom_ = zoom; }

    [[nodiscard]] constexpr const edt::Vec2f& GetEye() const { return eye_; }
    constexpr void SetEye(const edt::Vec2f& eye) { eye_ = eye; }

    [[nodiscard]] constexpr const edt::Mat3f& CameraToView() const { return camera_to_view_; }
    [[nodiscard]] constexpr const edt::Mat3f& ViewToCamera() const { return view_to_camera_; }
    [[nodiscard]] constexpr const edt::Mat3f& WorldToView() const { return world_to_view_; }
    [[nodiscard]] constexpr const edt::Mat3f& ViewToWorld() const { return view_to_world_; }

    constexpr void Update(float aspect_ratio)
    {
        edt::Vec2f half_camera_extent{1, 1};
        if (aspect_ratio > 1)
        {
            half_camera_extent.x() *= aspect_ratio;
        }
        else
        {
            half_camera_extent.y() /= aspect_ratio;
        }

        half_camera_extent /= GetZoom();

        // Forward
        world_to_camera_ = edt::Math::TranslationMatrix(-eye_);
        camera_to_view_ = edt::Math::ScaleMatrix(1.f / half_camera_extent);
        world_to_view_ = camera_to_view_.MatMul(world_to_camera_);

        // Backwards
        view_to_camera_ = edt::Math::ScaleMatrix(half_camera_extent);
        camera_to_world_ = edt::Math::TranslationMatrix(eye_);
        view_to_world_ = camera_to_world_.MatMul(view_to_camera_);
    }

private:
    float zoom_ = 1.f;
    edt::Vec2f eye_{};

    // Cache
    edt::Mat3f camera_to_view_ = {};
    edt::Mat3f view_to_camera_ = {};
    edt::Mat3f world_to_camera_ = {};
    edt::Mat3f camera_to_world_ = {};
    edt::Mat3f view_to_world_ = {};
    edt::Mat3f screen_to_view_ = {};
    edt::Mat3f world_to_view_ = {};
};
}  // namespace rect_tree_viewer
