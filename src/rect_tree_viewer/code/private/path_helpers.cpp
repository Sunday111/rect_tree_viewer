#include "path_helpers.hpp"

#include <filesystem>

#include "klgl/error_handling.hpp"

#ifdef WIN32

#include "Windows.h"

std::string PathHelpers::StringToUTF8(const std::wstring_view& wstr)
{
    // Determine the required buffer size for the UTF-8 string
    int utf8Length = WideCharToMultiByte(
        CP_UTF8,      // Code page: UTF-8
        0,            // Flags
        wstr.data(),  // Source wide string
        -1,           // Null-terminated input
        nullptr,      // No output buffer yet
        0,            // Get buffer size
        nullptr,
        nullptr  // Default character handling
    );

    klgl::ErrorHandling::Ensure(utf8Length != 0, "WideCharToMultiByte failed!");

    // Allocate the output string
    std::string utf8String(utf8Length - 1, '\0');  // Exclude the null terminator

    // Perform the conversion
    WideCharToMultiByte(
        CP_UTF8,
        0,
        wstr.data(),
        -1,
        utf8String.data(),  // Output buffer
        utf8Length,
        nullptr,
        nullptr);

    return utf8String;
}

[[nodiscard]] std::string PathHelpers::PathToUTF8(const std::filesystem::path& path)
{
    return StringToUTF8(path.wstring());
}

#else

std::string PathHelpers::StringToUTF8(const std::wstring_view& wstr)
{
    throw klgl::ErrorHandling::RuntimeErrorWithMessage("Not implemented");
}

[[nodiscard]] std::string PathHelpers::PathToUTF8(const std::filesystem::path& path)
{
    return path.string();
}

#endif
