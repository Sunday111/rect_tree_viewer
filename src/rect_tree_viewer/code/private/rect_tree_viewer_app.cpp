#include "rect_tree_viewer_app.hpp"

#include <random>
#include <ranges>

#include "klgl/events/event_listener_method.hpp"
#include "klgl/events/event_manager.hpp"
#include "klgl/opengl/gl_api.hpp"
#include "read_directory_tree.hpp"

namespace rect_tree_viewer
{

std::vector<Rect2d> MakeRectTreeRenderData(const std::span<const TreeNode> nodes, const float padding_factor)
{
    auto get_node_value = [&](size_t id)
    {
        return nodes[id].value;
    };

    std::vector<Rect2d> rects(nodes.size());
    rects[0] = {.bottom_left = {-1, -1}, .size = {2, 2}};

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

    std::vector<size_t> children_nodes;
    std::vector<Region> regions;
    for (const size_t node_id : std::views::iota(size_t{0}, nodes.size()))
    {
        children_nodes.clear();
        regions.clear();
        TreeHelper::GetChildren(nodes, node_id, children_nodes);

        if (children_nodes.empty())
        {
            continue;
        }

        // sort children by value (=> area) in descending manner.
        std::ranges::sort(children_nodes, std::greater{}, get_node_value);

        {
            const auto& orig_rect = rects[node_id];
            const Vec2f rect_size = orig_rect.size * padding_factor;
            regions.push_back(
                {.rect = {.bottom_left = orig_rect.bottom_left + (orig_rect.size - rect_size) / 2, .size = rect_size},
                 .nodes = children_nodes,
                 .value = get_node_value(node_id)});
        }

        while (!regions.empty())
        {
            auto region_to_split = regions.back();
            regions.pop_back();

            if (region_to_split.nodes.size() == 1)
            {
                rects[region_to_split.nodes.front()] = region_to_split.rect;
                continue;
            }

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

            Region first_region{
                .rect = first_rect,
                .nodes = region_to_split.nodes.subspan(0, first_region_size),
                .value = first_region_value,
            };
            assert(first_region.nodes.size() != 0);

            Region second_region{
                .rect = second_rect,
                .nodes = region_to_split.nodes.subspan(first_region_size),
                .value = region_to_split.value - first_region_value,
            };
            assert(second_region.nodes.size() != 0);

            regions.push_back(first_region);
            regions.push_back(second_region);
        }
    }

    return rects;
}

void RectTreeViewerApp::Initialize()
{
    event_listener_ = klgl::events::EventListenerMethodCallbacks<&RectTreeViewerApp::OnMouseScroll>::CreatePtr(this);
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

    if (root_paths_.size() == 1)
    {
        nodes_ = ReadDirectoryTreeMulti(std::nullopt, root_paths_, &root_node_id_to_path_index_);
    }
    else
    {
        nodes_ = ReadDirectoryTreeMulti("SELECTION", root_paths_, &root_node_id_to_path_index_);
    }

    rects_ = MakeRectTreeRenderData(nodes_);

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

void RectTreeViewerApp::OnMouseScroll(const klgl::events::OnMouseScroll& event)
{
    if (!ImGui::GetIO().WantCaptureMouse)
    {
        zoom_power_ += event.value.y();
        camera_.zoom = std::max(std::pow(1.1f, zoom_power_), 0.1f);
    }
}

void RectTreeViewerApp::UpdateCamera()
{
    if (!with_custom_viewport_) viewport_.MatchWindowSize(GetWindow().GetSize2f());
    viewport_.UseInOpenGL();

    transforms_.Update(camera_, viewport_, kAspectRatioPolicy);

    if (!ImGui::GetIO().WantCaptureKeyboard)
    {
        Vec2f offset{};
        if (ImGui::IsKeyDown(ImGuiKey_W)) offset.y() += 1.f;
        if (ImGui::IsKeyDown(ImGuiKey_S)) offset.y() -= 1.f;
        if (ImGui::IsKeyDown(ImGuiKey_D)) offset.x() += 1.f;
        if (ImGui::IsKeyDown(ImGuiKey_A)) offset.x() -= 1.f;

        constexpr float pan_speed = 0.2f;
        camera_.eye += (GetLastFrameDurationSeconds() * offset) * pan_speed;
    }
}

Vec2f RectTreeViewerApp::GetMousePositionInWorldCoordinates() const
{
    const auto screen_size = GetWindow().GetSize2f();
    Vec2f p = FromImVec(ImGui::GetMousePos());
    p.y() = screen_size.y() - p.y();
    return edt::Math::TransformPos(transforms_.screen_to_world, p);
}

std::optional<size_t> RectTreeViewerApp::FindNodeAt(const Vec2f& position) const
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

std::string RectTreeViewerApp::GetNodeFullPath(size_t in_node_id) const
{
    std::string path;
    std::string buffer;

    std::optional<size_t> node_id = in_node_id;
    while (node_id)
    {
        auto& node = nodes_[*node_id];
        auto inserter = std::back_inserter(buffer);
        const bool is_root = root_node_id_to_path_index_.contains(*node_id);

        const auto format = fmt::runtime(path.empty() ? "{}" : "{}/{}");

        if (is_root)
        {
            fmt::format_to(inserter, format, root_paths_[root_node_id_to_path_index_.at(*node_id)], path);
        }
        else
        {
            fmt::format_to(inserter, format, node.name, path);
        }

        std::swap(path, buffer);
        buffer.clear();
        node_id = is_root ? std::nullopt : node.parent;
    }

    for (char& c : path)
    {
        if (c == '\\') c = '/';
    }

    return path;
}

std::tuple<long double, std::string_view> RectTreeViewerApp::PickSizeUnit(long double size)
{
    if (auto value = (size / 1'000'000'000'000'000); value > 1.001L) return {value, "PB"};
    if (auto value = (size / 1'000'000'000'000); value > 1.001L) return {value, "TB"};
    if (auto value = (size / 1'000'000'000); value > 1.001L) return {value, "GB"};
    if (auto value = (size / 1'000'000); value > 1.001L) return {value, "MB"};
    if (auto value = (size / 1'000); value > 1.001L) return {value, "kB"};
    return {size, "b"};
}

void RectTreeViewerApp::DrawGUI()
{
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
    }
}

void RectTreeViewerApp::Tick()
{
    UpdateCamera();

    painter_->BeginDraw();

    painter_->SetViewMatrix(transforms_.world_to_view.Transposed());

    for (const size_t i : std::views::iota(size_t{0}, nodes_.size()))
    {
        painter_->DrawRect(rects_[i].ToPainterRect(colors_[i]));
    }

    painter_->EndDraw();

    DrawGUI();
}

}  // namespace rect_tree_viewer
