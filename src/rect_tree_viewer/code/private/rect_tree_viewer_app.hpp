#pragma once

#include <imgui.h>

#include <EverydayTools/Math/Math.hpp>
#include <filesystem>
#include <klgl/ui/simple_type_widget.hpp>
#include <optional>

#include "fmt/std.h"  // IWYU pragma: keep
#include "klgl/application.hpp"
#include "klgl/camera/camera_2d.hpp"
#include "klgl/error_handling.hpp"
#include "klgl/events/event_listener_interface.hpp"
#include "klgl/events/mouse_events.hpp"
#include "klgl/filesystem/filesystem.hpp"
#include "klgl/reflection/matrix_reflect.hpp"  // IWYU pragma: keep
#include "klgl/rendering/painter2d.hpp"
#include "klgl/window.hpp"
#include "nlohmann/json.hpp"
#include "tree.hpp"

namespace rect_tree_viewer
{

namespace fs = std::filesystem;
using namespace edt::lazy_matrix_aliases;  // NOLINT

[[nodiscard]] static constexpr ImVec2 ToImVec(Vec2f v) noexcept
{
    return ImVec2(v.x(), v.y());
}

[[nodiscard]] static constexpr Vec2f FromImVec(ImVec2 v) noexcept
{
    return {v.x, v.y};
}

inline void WriteNodesGraphToJSON(std::span<const TreeNode> nodes, const fs::path& path)
{
    nlohmann::json json;
    auto& nodes_json = json["nodes"];
    for (const TreeNode& node : nodes)
    {
        nlohmann::json& node_json = nodes_json.emplace_back(nlohmann::json::object());
        node_json["name"] = node.name;
        node_json["value"] = node.value;

        if (node.parent) node_json["parent"] = *node.parent;
        if (node.first_child) node_json["first_child"] = *node.first_child;
        if (node.next_sibling) node_json["next_sibling"] = *node.next_sibling;
    }

    klgl::Filesystem::WriteFile(path, json.dump(2, ' '));
}

struct Rect2d
{
    Vec2f bottom_left{};
    Vec2f size{};

    [[nodiscard]] constexpr klgl::Painter2d::Rect2d ToPainterRect(edt::Vec4u8 color) const
    {
        return {
            .center = bottom_left + size / 2,
            .size = size,
            .color = color,
            .rotation_degrees = 0.f,
        };
    }

    [[nodiscard]] constexpr bool Contains(const Vec2f& position) const
    {
        auto d = position - bottom_left;

        return d.x() >= 0 && d.x() <= size.x() && d.y() >= 0 && d.y() <= size.y();
    }
};

struct Region
{
    Rect2d rect;
    std::span<const size_t> nodes;
    long double value = 0;
};

class RectTreeViewerApp : public klgl::Application
{
public:
    static constexpr auto kAspectRatioPolicy = klgl::AspectRatioPolicy::Stretch;

    explicit RectTreeViewerApp(std::vector<fs::path> paths) : klgl::Application(), root_paths_(std::move(paths))
    {
        klgl::ErrorHandling::Ensure(!root_paths_.empty(), "Expected at least one path");
    }

    void Initialize() override;
    void OnMouseScroll(const klgl::events::OnMouseScroll& event);
    void UpdateCamera();
    Vec2f GetMousePositionInWorldCoordinates() const;
    std::optional<size_t> FindNodeAt(const Vec2f& position) const;
    std::string GetNodeFullPath(size_t in_node_id) const;
    static std::tuple<long double, std::string_view> PickSizeUnit(long double size);
    void DrawGUI();
    void Tick() override;

    template <typename... Args>
    constexpr static void FormatToBuffer(std::string& buffer, fmt::format_string<Args...> format_string, Args&&... args)
    {
        fmt::format_to(std::back_inserter(buffer), format_string, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void ImGuiText(fmt::format_string<Args...> format_string, Args&&... args)
    {
        text_buffer_.clear();
        FormatToBuffer(text_buffer_, format_string, std::forward<Args>(args)...);
        const char* begin = text_buffer_.data();
        const char* end = begin + text_buffer_.size();  // NOLINT
        ImGui::TextUnformatted(begin, end);
    }

    std::unique_ptr<klgl::events::IEventListener> event_listener_;
    ImFont* big_font_ = nullptr;

    std::string text_buffer_;

    std::vector<TreeNode> nodes_;
    std::unordered_map<size_t, size_t> root_node_id_to_path_index_;

    std::vector<Rect2d> rects_;
    std::vector<Vec4u8> colors_;
    std::unique_ptr<klgl::Painter2d> painter_;
    std::vector<fs::path> root_paths_;

    float zoom_power_ = 0.f;

    klgl::Camera2d camera_;
    klgl::Viewport viewport_;
    klgl::RenderTransforms2d transforms_;
    bool with_custom_viewport_ = false;
};
}  // namespace rect_tree_viewer
