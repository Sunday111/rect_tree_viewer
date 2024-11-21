#pragma once

#include <filesystem>
#include <string>
#include <string_view>

class PathHelpers
{
public:
    [[nodiscard]] static std::string StringToUTF8(const std::wstring_view& wstr);
    [[nodiscard]] static std::string PathToUTF8(const std::filesystem::path& path);
};
