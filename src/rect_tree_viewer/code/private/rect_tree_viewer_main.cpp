#include <imgui.h>

#include <EverydayTools/Math/Math.hpp>
#include <filesystem>
#include <klgl/ui/simple_type_widget.hpp>
#include <optional>
#include <random>
#include <ranges>

#include "EverydayTools/Math/FloatRange.hpp"
#include "camera_2d.hpp"
#include "fmt/std.h"  // IWYU pragma: keep
#include "klgl/application.hpp"
#include "klgl/error_handling.hpp"
#include "klgl/events/event_listener_interface.hpp"
#include "klgl/events/event_listener_method.hpp"
#include "klgl/events/event_manager.hpp"
#include "klgl/events/mouse_events.hpp"
#include "klgl/filesystem/filesystem.hpp"
#include "klgl/opengl/gl_api.hpp"
#include "klgl/rendering/painter2d.hpp"
#include "klgl/window.hpp"
#include "nlohmann/json.hpp"
#include "read_directory_tree.hpp"
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

void WriteNodesGraphToJSON(std::span<const TreeNode> nodes, const fs::path& path)
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

class Viewport
{
public:
    constexpr void FullWindow(const Vec2f& window_size) { range = edt::FloatRange2Df::FromMinMax({}, window_size); }
    [[nodiscard]] constexpr float GetAspect() const
    {
        auto extent = range.Extent();
        return extent.x() / extent.y();
    }

    void UseInOpenGL()
    {
        auto extent = range.Extent();
        glViewport(
            static_cast<GLint>(range.x.begin),
            static_cast<GLint>(range.y.begin),
            static_cast<GLint>(extent.x()),
            static_cast<GLint>(extent.y()));
    }

    [[nodiscard]] constexpr Vec2f ScreenToView(const Vec2f p) const
    {
        return (2 * (p - range.Min()) / range.Extent()) - 1;
    }

    edt::FloatRange2Df range;
};

class RectTreeViewerApp : public klgl::Application
{
    void Initialize() override
    {
        event_listener_ =
            klgl::events::EventListenerMethodCallbacks<&RectTreeViewerApp::OnMouseScroll>::CreatePtr(this);
        GetEventManager().AddEventListener(*event_listener_);

        klgl::Application::Initialize();
        klgl::OpenGl::SetClearColor({});
        GetWindow().SetSize(1000, 1000);
        GetWindow().SetTitle("Painter 2d");
        SetTargetFramerate(60.f);
        painter_ = std::make_unique<klgl::Painter2d>();

        big_font_ = [&](float pixel_size)
        {
            ImGuiIO& io = ImGui::GetIO();
            ImFontConfig config;
            config.SizePixels = pixel_size;
            config.OversampleH = config.OversampleV = 1;
            config.PixelSnapH = true;
            ImFont* font = io.Fonts->AddFontDefault(&config);
            return font;
        }(45);

        nodes_ = ReadDirectoryTree(root_path_);

        constexpr float shrink_factor = 0.97f;

        rects_.resize(nodes_.size());
        rects_[0] = {.bottom_left = {-1, -1}, .size = {2, 2}};

        std::vector<size_t> children_nodes;
        std::vector<Region> regions;
        auto handle_children_of_node = [&](const size_t node_id)
        {
            children_nodes.clear();
            regions.clear();
            TreeHelper::GetChildren(nodes_, node_id, children_nodes);

            if (children_nodes.empty())
            {
                return;
            }

            // sort children by value (=> area) in descending manner.
            std::ranges::sort(children_nodes, std::greater{}, [&](size_t id) { return nodes_[id].value; });

            {
                const auto& orig_rect = rects_[node_id];
                Vec2f rect_size = orig_rect.size * shrink_factor;
                regions.push_back(
                    {.rect =
                         {.bottom_left = orig_rect.bottom_left + (orig_rect.size - rect_size) / 2, .size = rect_size},
                     .nodes = children_nodes,
                     .value = nodes_[node_id].value});
            }

            while (!regions.empty())
            {
                auto region_to_split = regions.back();
                regions.pop_back();

                if (region_to_split.nodes.size() == 1)
                {
                    rects_[region_to_split.nodes.front()] = region_to_split.rect;
                    continue;
                }

                size_t first_region_size = 1;
                long double first_region_value = nodes_[region_to_split.nodes.front()].value;
                while (first_region_value * 2.02 < region_to_split.value)
                {
                    size_t child_id = region_to_split.nodes[first_region_size];
                    first_region_value += nodes_[child_id].value;
                    first_region_size++;
                }

                Region first_region{};
                first_region.nodes = region_to_split.nodes.subspan(0, first_region_size);
                assert(first_region.nodes.size() != 0);
                first_region.value = first_region_value;

                Region second_region{};
                second_region.nodes = region_to_split.nodes.subspan(first_region_size);
                assert(second_region.nodes.size() != 0);
                second_region.value = region_to_split.value - first_region_value;

                const long double split_ratio = first_region_value / region_to_split.value;
                if (region_to_split.rect.size.x() > region_to_split.rect.size.y())
                {
                    // split along X
                    const auto& r = region_to_split.rect;
                    // left rect
                    float left_width = static_cast<float>(r.size.x() * split_ratio);
                    first_region.rect = {
                        .bottom_left = r.bottom_left,
                        .size = {left_width, r.size.y()},
                    };
                    // right rect
                    second_region.rect = {
                        .bottom_left = {r.bottom_left.x() + left_width, r.bottom_left.y()},
                        .size = {r.size.x() - left_width, r.size.y()},
                    };
                }
                else
                {
                    // split along Y
                    const auto& r = region_to_split.rect;
                    // bottom rect
                    float bottom_height = static_cast<float>(r.size.y() * split_ratio);
                    first_region.rect = {
                        .bottom_left = r.bottom_left,
                        .size = {r.size.x(), bottom_height},
                    };
                    // top rect
                    second_region.rect = {
                        .bottom_left = {r.bottom_left.x(), r.bottom_left.y() + bottom_height},
                        .size = {r.size.x(), r.size.y() - bottom_height},
                    };
                }

                regions.push_back(first_region);
                regions.push_back(second_region);
            }
        };

        for (const size_t node_id : std::views::iota(size_t{0}, nodes_.size()))
        {
            handle_children_of_node(node_id);
        }

        colors_.resize(nodes_.size());
        unsigned kSeed = 0;
        std::mt19937 rnd(kSeed);  // NOLINT
        std::uniform_int_distribution<int> color_distribution(0, 255);
        auto get_color_value = [&]
        {
            return static_cast<uint8_t>(color_distribution(rnd));
        };
        for (const size_t i : std::views::iota(size_t{0}, nodes_.size()))
        {
            colors_[i] = {get_color_value(), get_color_value(), get_color_value(), 255};
        }
    }

    void OnMouseScroll(const klgl::events::OnMouseScroll& event)
    {
        if (!ImGui::GetIO().WantCaptureMouse)
        {
            zoom_power_ += event.value.y();
            float zoom = std::powf(1.1f, zoom_power_);
            zoom = std::max(zoom, 0.1f);
            camera_.SetZoom(zoom);
        }
    }

    void UpdateCamera()
    {
        if (!with_custom_viewport_) viewport_.FullWindow(GetWindow().GetSize2f());
        camera_.Update(viewport_.GetAspect());
        viewport_.UseInOpenGL();

        if (!ImGui::GetIO().WantCaptureKeyboard)
        {
            Vec2f offset{};
            if (ImGui::IsKeyDown(ImGuiKey_W)) offset.y() += 1.f;
            if (ImGui::IsKeyDown(ImGuiKey_S)) offset.y() -= 1.f;
            if (ImGui::IsKeyDown(ImGuiKey_D)) offset.x() += 1.f;
            if (ImGui::IsKeyDown(ImGuiKey_A)) offset.x() -= 1.f;

            Vec2f eye = camera_.GetEye();
            constexpr float pan_speed = 0.2f;
            eye += (GetLastFrameDurationSeconds() * offset) * pan_speed;
            camera_.SetEye(eye);
        }
    }

    Vec2f GetMousePositionInWorldCoordinates() const
    {
        const auto screen_size = GetWindow().GetSize2f();
        Vec2f p = FromImVec(ImGui::GetMousePos());
        p.y() = screen_size.y() - p.y();
        p = viewport_.ScreenToView(p);
        return edt::Math::TransformPos(camera_.ViewToWorld(), p);
    }

    std::optional<size_t> FindNodeAt(const Vec2f& position) const
    {
        if (rects_.empty() || !rects_.front().Contains(position)) return std::nullopt;

        size_t parent = 0;
        std::vector<size_t> children;

        while (true)
        {
            children.clear();
            TreeHelper::GetChildren(nodes_, parent, children);

            bool found_child = false;
            for (size_t child : children)
            {
                if (rects_[child].Contains(position))
                {
                    found_child = true;
                    parent = child;
                    break;
                }
            }

            if (!found_child)
            {
                break;
            }
        }

        return parent;
    }

    std::string GetNodeFullPath(size_t in_node_id) const
    {
        std::string path = nodes_[in_node_id].name;
        std::string buffer;

        std::optional<size_t> node_id = nodes_[in_node_id].parent;
        while (node_id)
        {
            auto& node = nodes_[*node_id];
            fmt::format_to(std::back_inserter(buffer), "{}/{}", node.name, path);
            std::swap(path, buffer);
            buffer.clear();
            node_id = node.parent;
        }

        return path;
    }

    std::tuple<long double, std::string_view> PickSizeUnit(long double size)
    {
        if (auto value = (size / 1'000'000'000'000'000); value > 1.001) return {value, "PB"};
        if (auto value = (size / 1'000'000'000'000); value > 1.001) return {value, "TB"};
        if (auto value = (size / 1'000'000'000); value > 1.001) return {value, "GB"};
        if (auto value = (size / 1'000'000); value > 1.001) return {value, "MB"};
        if (auto value = (size / 1'000); value > 1.001) return {value, "kB"};
        return {size, "b"};
    }

    void DrawGUI()
    {
        const Vec2f window_padding{10, 10};

        ImGui::PushFont(big_font_);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ToImVec(window_padding));

        const Vec2f root_window_size = GetWindow().GetSize2f();
        const Vec2f panel_size = root_window_size * Vec2f{1.f, 0.15f};
        const Vec2f panel_position{0, root_window_size.y() - panel_size.y()};

        constexpr int flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                              ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

        ImGui::SetNextWindowPos(ToImVec(panel_position));
        ImGui::SetNextWindowSize(ToImVec(panel_size));

        if (ImGui::Begin("Counter", nullptr, flags))
        {
            ImGuiText("Root: {}", root_path_);
            if (auto opt_node_id = FindNodeAt(GetMousePositionInWorldCoordinates()))
            {
                ImGuiText("Cursor: {}", GetNodeFullPath(*opt_node_id));

                const auto [value, unit] = PickSizeUnit(nodes_[*opt_node_id].value);
                ImGuiText("  Size: {} {}", value, unit);
            }
            ImGui::End();
        }

        ImGui::PopStyleVar(2);
        ImGui::PopFont();

        if (ImGui::Begin("Viewport"))
        {
            ImGui::Checkbox("Custom", &with_custom_viewport_);
            if (with_custom_viewport_)
            {
                Vec2f min = viewport_.range.Min();
                klgl::SimpleTypeWidget("Min", min);

                Vec2f max = viewport_.range.Max();
                klgl::SimpleTypeWidget("Max", max);

                viewport_.range = edt::FloatRange2Df::FromMinMax(min, max);
            }
            ImGui::End();
        }
    }

    void Tick() override
    {
        UpdateCamera();

        painter_->BeginDraw();

        painter_->SetViewMatrix(camera_.WorldToView().Transposed());

        for (const size_t i : std::views::iota(size_t{0}, nodes_.size()))
        {
            painter_->DrawRect(rects_[i].ToPainterRect(colors_[i]));
        }

        painter_->EndDraw();

        DrawGUI();
    }

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

    ImFont* big_font_ = nullptr;

    std::string text_buffer_;

    std::vector<TreeNode> nodes_;
    std::vector<Rect2d> rects_;
    std::vector<Vec4u8> colors_;
    std::unique_ptr<klgl::Painter2d> painter_;
    fs::path root_path_ = fs::path("C:/data/projects/verlet");

    float zoom_power_ = 0.f;

    Camera2d camera_;
    std::unique_ptr<klgl::events::IEventListener> event_listener_;

    Viewport viewport_{};
    bool with_custom_viewport_ = false;
};

void Main()
{
    RectTreeViewerApp app;
    app.Run();
}
}  // namespace rect_tree_viewer

int main()
{
    klgl::ErrorHandling::InvokeAndCatchAll(rect_tree_viewer::Main);
}
