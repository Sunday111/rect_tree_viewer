#include <imgui.h>

#include <EverydayTools/Math/Math.hpp>
#include <filesystem>
#include <optional>
#include <random>
#include <ranges>

#include "EverydayTools/Math/FloatRange.hpp"
#include "camera.hpp"
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

namespace rect_tree_viewer
{

namespace fs = std::filesystem;
using namespace edt::lazy_matrix_aliases;  // NOLINT

struct Node
{
    std::string name;
    long double value;
    std::optional<size_t> parent{};
    std::optional<size_t> first_child{};
    std::optional<size_t> next_sibling{};

    void GetChildren(std::span<const Node> nodes, std::vector<size_t>& out_children) const
    {
        std::optional<size_t> opt_child_id = first_child;
        while (opt_child_id)
        {
            size_t child_id = opt_child_id.value();
            out_children.push_back(child_id);
            opt_child_id = nodes[child_id].next_sibling;
        }
    }
};

[[nodiscard]] std::vector<Node> MakeNodeGraph(const fs::path& root_path)
{
    struct WalkEntry
    {
        fs::directory_entry dir_entry;
        size_t id;
    };

    std::vector<Node> nodes;

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
            Node& node = nodes[walk_entry.id];
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
    for (Node& node : nodes | std::views::reverse | std::views::take(nodes.size() - 1))
    {
        [[likely]] if (node.parent)
        {
            nodes[*node.parent].value += node.value;
        }
    }

    return nodes;
}

void WriteNodesGraphToJSON(std::span<const Node> nodes, const fs::path& path)
{
    nlohmann::json json;
    auto& nodes_json = json["nodes"];
    for (const Node& node : nodes)
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
    void Initialize() override
    {
        event_listener_ =
            klgl::events::EventListenerMethodCallbacks<&RectTreeViewerApp::OnMouseScroll>::CreatePtr(this);
        GetEventManager().AddEventListener(*event_listener_);

        klgl::Application::Initialize();
        klgl::OpenGl::SetClearColor({});
        GetWindow().SetSize(1000, 1000);
        GetWindow().SetTitle("Painter 2d");
        painter_ = std::make_unique<klgl::Painter2d>();
        nodes_ = MakeNodeGraph(root_path_);

        constexpr float shrink_factor = 0.97f;

        rects_.resize(nodes_.size());
        rects_[0] = {.bottom_left = {-1, -1}, .size = {2, 2}};

        std::vector<size_t> children_nodes;
        std::vector<Region> regions;
        auto handle_children_of_node = [&](const size_t node_id)
        {
            children_nodes.clear();
            regions.clear();
            nodes_[node_id].GetChildren(nodes_, children_nodes);

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
            camera_.Zoom(event.value.y() * camera_.zoom_speed);
        }
    }

    void UpdateCamera()
    {
        camera_.Update(kWorldRange);

        if (!ImGui::GetIO().WantCaptureKeyboard)
        {
            Vec2f offset{};
            if (ImGui::IsKeyDown(ImGuiKey_W)) offset.y() += 1.f;
            if (ImGui::IsKeyDown(ImGuiKey_S)) offset.y() -= 1.f;
            if (ImGui::IsKeyDown(ImGuiKey_D)) offset.x() += 1.f;
            if (ImGui::IsKeyDown(ImGuiKey_A)) offset.x() -= 1.f;

            camera_.Pan((GetLastFrameDurationSeconds() * camera_.GetRange().Extent() * offset) * camera_.pan_speed);
        }
    }

    void UpdateRenderTransforms()
    {
        const auto screen_range = edt::FloatRange2Df::FromMinMax({}, GetWindow().GetSize2f());
        const auto camera_to_world_vector = kWorldRange.Uniform(.5f) - camera_.GetEye();
        const auto camera_extent = camera_.GetRange().Extent();

        world_to_camera_ = edt::Math::TranslationMatrix(camera_to_world_vector);
        auto camera_to_view_ = edt::Math::ScaleMatrix(kViewRange.Extent() / camera_extent);
        world_to_view_ = camera_to_view_.MatMul(world_to_camera_);

        const auto screen_to_view =
            edt::Math::TranslationMatrix(Vec2f{} - 1).MatMul(edt::Math::ScaleMatrix(2 / screen_range.Extent()));
        const auto view_to_camera = edt::Math::ScaleMatrix(camera_extent / kViewRange.Extent());
        const auto camera_to_world = edt::Math::TranslationMatrix(-camera_to_world_vector);
        screen_to_world_ = camera_to_world.MatMul(view_to_camera).MatMul(screen_to_view);
    }

    Vec2f GetMousePositionInWorldCoordinates() const
    {
        const auto screen_range = edt::FloatRange2Df::FromMinMax({}, GetWindow().GetSize2f());  // 0 -> resolution
        auto [x, y] = ImGui::GetMousePos();
        y = screen_range.y.Extent() - y;
        return edt::Math::TransformPos(screen_to_world_, Vec2f{x, y});
    }

    std::optional<size_t> FindNodeAt(const Vec2f& position) const
    {
        if (rects_.empty() || !rects_.front().Contains(position)) return std::nullopt;

        size_t parent = 0;
        std::vector<size_t> children;

        while (true)
        {
            children.clear();
            nodes_[parent].GetChildren(nodes_, children);

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

    void Tick() override
    {
        UpdateCamera();
        UpdateRenderTransforms();

        painter_->BeginDraw();

        painter_->SetViewMatrix(world_to_view_.Transposed());

        for (const size_t i : std::views::iota(size_t{0}, nodes_.size()))
        {
            painter_->DrawRect(rects_[i].ToPainterRect(colors_[i]));
        }

        auto mouse_pos = GetMousePositionInWorldCoordinates();
        if (auto opt_node_id = FindNodeAt(mouse_pos))
        {
            std::string path = GetNodeFullPath(*opt_node_id);
            ImGui::Text("%s: %zu", path.data(), static_cast<size_t>(nodes_[*opt_node_id].value));  // NOLINT
        }

        painter_->EndDraw();
    }

    static constexpr auto kWorldRange = edt::FloatRange2Df::FromMinMax(Vec2f{} - 1, Vec2f{} + 1);
    static constexpr auto kViewRange = edt::FloatRange2Df::FromMinMax(Vec2f{} - 1, Vec2f{} + 1);

    std::vector<Node> nodes_;
    std::vector<Rect2d> rects_;
    std::vector<Vec4u8> colors_;
    std::unique_ptr<klgl::Painter2d> painter_;
    fs::path root_path_ = fs::path("C:/data/projects");

    Mat3f world_to_camera_;
    Mat3f world_to_view_;

    Mat3f screen_to_world_;
    Camera camera_;
    std::unique_ptr<klgl::events::IEventListener> event_listener_;
};

void Main()
{
    RectTreeViewerApp app;
    app.Run();
}
}  // namespace rect_tree_viewer

int main()
{
    klgl::ErrorHandling::InvokeAndCatchAll(dir_to_tree::Main);
}
