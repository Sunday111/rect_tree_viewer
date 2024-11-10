#include <imgui.h>

#include <EverydayTools/Math/Math.hpp>
#include <filesystem>
#include <klgl/ui/simple_type_widget.hpp>
#include <ranges>

#include "fmt/std.h"  // IWYU pragma: keep
#include "klgl/error_handling.hpp"
#include "klgl/reflection/matrix_reflect.hpp"  // IWYU pragma: keep
#include "rect_tree_viewer_app.hpp"

#ifdef _WIN32
#include "open_file_dialog.hpp"
#endif

#include "tl/expected.hpp"

namespace rect_tree_viewer
{

// base and target paths has to be absolute
bool IsRelativeTo(const fs::path& path, const fs::path& parent_path)
{
    auto parent_it = parent_path.begin();
    auto path_it = path.begin();

    for (; parent_it != parent_path.end(); ++parent_it, ++path_it)
    {
        if (path_it == path.end() || *path_it != *parent_it) return false;
    }

    return true;
}

tl::expected<std::vector<fs::path>, std::string> ParseCLI(int argc, char** argv)
{
    std::vector<fs::path> paths;
    paths.reserve(static_cast<size_t>(argc) - 1);

    for (const int arg_index : std::views::iota(0, argc) | std::views::drop(1))
    {
        fs::path path{argv[arg_index]};  // NOLINT

        path = fs::absolute(path);

        if (!fs::exists(path))
        {
            return tl::make_unexpected(fmt::format("Path \"{}\" does not exist", path));
        }

        if (!fs::is_directory(path))
        {
            return tl::make_unexpected(fmt::format("Path \"{}\" is not a directory", path));
        }

        paths.push_back(std::move(path));
    }

    return {tl::in_place, paths};
}

tl::expected<std::vector<fs::path>, std::string> TakePathsFromDialogIfNoCLI(std::vector<fs::path> paths)
{
    if (paths.empty())
    {
#ifdef _WIN32
        try
        {
            paths = OpenFileDialog({.multiselect = true, .pick_folders = true});
        }
        catch (const cpptrace::exception_with_message& ex)
        {
            return tl::unexpected(ex.message());
        }
        catch (const std::exception& ex)
        {
            return tl::unexpected(ex.what());
        }
#endif
    }

    return {tl::in_place, paths};
}

int Main(int argc, char** argv)
{
    if (const auto maybe_paths = ParseCLI(argc, argv).and_then(TakePathsFromDialogIfNoCLI); maybe_paths.has_value())
    {
        RectTreeViewerApp app(maybe_paths.value());
        app.Run();
        return 0;
    }
    else
    {
        fmt::println(stderr, "Error: {}", maybe_paths.error());
        return 1;
    }
}

}  // namespace rect_tree_viewer

int main(int argc, char** argv)
{
    return klgl::ErrorHandling::InvokeAndCatchAll(rect_tree_viewer::Main, argc, argv);
}
