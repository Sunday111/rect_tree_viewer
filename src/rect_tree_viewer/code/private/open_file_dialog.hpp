#pragma once

#include <filesystem>
#include <vector>

class OpenFileDialogParams
{
public:
    bool multiselect = false;
    bool pick_folders = false;
};

std::vector<std::filesystem::path> OpenFileDialog(const OpenFileDialogParams& params);
