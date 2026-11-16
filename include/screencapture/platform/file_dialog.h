#pragma once
#include <gtk/gtk.h>
#include <filesystem>
#include <string>

namespace sc::platform {

class FileDialog {
public:
    // Native GtkFileChooserNative save dialog. Returns empty path on cancel.
    static std::filesystem::path saveImage(const std::string& ext = "png",
                                           GtkWindow* parent = nullptr);
    // Native folder picker (SELECT_FOLDER). Returns the chosen dir or "".
    static std::string chooseDirectory(const std::string& current = {},
                                       GtkWindow* parent = nullptr);
    static std::string createFileName(const std::string& ext);
};

} // namespace sc::platform
