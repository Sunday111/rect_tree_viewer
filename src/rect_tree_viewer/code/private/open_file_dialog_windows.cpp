#ifdef _WIN32

#include <shobjidl.h>  // Required for IFileOpenDialog
#include <windows.h>

#include "klgl/error_handling.hpp"
#include "klgl/template/on_scope_leave.hpp"
#include "open_file_dialog.hpp"

std::vector<std::filesystem::path> OpenFileDialog(const OpenFileDialogParams& params)
{
    std::vector<std::filesystem::path> files;

    klgl::ErrorHandling::Ensure(
        SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)),
        "CoInitializeEx failed");

    const auto uninit_guard = klgl::OnScopeLeave(CoUninitialize);

    // Create the FileOpenDialog object.
    IFileOpenDialog* open_file_dialog = nullptr;
    klgl::ErrorHandling::Ensure(
        SUCCEEDED(CoCreateInstance(
            CLSID_FileOpenDialog,
            NULL,
            CLSCTX_ALL,
            IID_IFileOpenDialog,
            reinterpret_cast<void**>(&open_file_dialog))),  // NOLINT
        "Failed to create open file dialog instance");

    const auto dialog_release = klgl::OnScopeLeave([&] { open_file_dialog->Release(); });

    // Set the options on the dialog
    DWORD options{};

    klgl::ErrorHandling::Ensure(
        SUCCEEDED(open_file_dialog->GetOptions(&options)),
        "Failed to get options for IFileOpenDialog");

    if (params.multiselect) options |= FOS_ALLOWMULTISELECT;
    if (params.pick_folders) options |= FOS_PICKFOLDERS;

    klgl::ErrorHandling::Ensure(
        SUCCEEDED(open_file_dialog->SetOptions(options)),
        "Failed to update options for IFileOpenDialog");

    klgl::ErrorHandling::Ensure(SUCCEEDED(open_file_dialog->Show(nullptr)), "Failed to open IFileOpenDialog");

    // Get the file names from the dialog box.
    IShellItemArray* item_array = nullptr;
    klgl::ErrorHandling::Ensure(
        SUCCEEDED(open_file_dialog->GetResults(&item_array)),
        "Failed to get results from IFileOpenDialog");

    const auto item_array_release = klgl::OnScopeLeave([&] { item_array->Release(); });

    DWORD count = 0;
    item_array->GetCount(&count);

    for (DWORD i = 0; i < count; i++)
    {
        IShellItem* item = nullptr;
        klgl::ErrorHandling::Ensure(
            SUCCEEDED(item_array->GetItemAt(i, &item)),
            "Failed to get item {} from item array",
            i);

        const auto item_release = klgl::OnScopeLeave([&] { item->Release(); });

        PWSTR file_path = NULL;
        klgl::ErrorHandling::Ensure(
            SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &file_path)),
            "failed to get display name of item {}",
            i);

        const auto memory_release = klgl::OnScopeLeave([&] { CoTaskMemFree(file_path); });

        files.emplace_back(file_path);
    }

    return files;
}

#endif
