#include "rect_tree_viewer_app.hpp"

#include <random>
#include <ranges>

#include "klgl/events/event_listener_method.hpp"
#include "klgl/events/event_manager.hpp"
#include "klgl/opengl/gl_api.hpp"
#include "read_directory_tree.hpp"

namespace rect_tree_viewer
{

void RectTreeViewerApp::Initialize()
{
    event_listener_ = klgl::events::EventListenerMethodCallbacks<&RectTreeViewerApp::OnMouseScroll>::CreatePtr(this);
    GetEventManager().AddEventListener(*event_listener_);

    klgl::Application::Initialize();
    klgl::OpenGl::SetClearColor({});
    GetWindow().SetSize(1000, 1000);
    GetWindow().SetTitle("Rect Tree Viewer");
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

    rects_ = RectTreeDrawData::Create(nodes_);

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
