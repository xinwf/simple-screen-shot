#include "screencapture/platform/file_dialog.h"
#include <chrono>
#include <cstdio>
#include <ctime>
#include <glib/gstdio.h>

namespace sc::platform {

std::string FileDialog::createFileName(const std::string& ext) {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()).count() % 1000;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "ScreenCapture_%04d%02d%02d_%02d%02d%02d_%03d.%s",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms), ext.c_str());
    return buf;
}

std::filesystem::path FileDialog::saveImage(const std::string& ext, GtkWindow* parent) {
    GtkFileChooserNative* dialog = gtk_file_chooser_native_new(
        "Save Screenshot", parent, GTK_FILE_CHOOSER_ACTION_SAVE, "_Save", "_Cancel");
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
    auto defaultName = createFileName(ext);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), defaultName.c_str());

    gint result = gtk_native_dialog_run(GTK_NATIVE_DIALOG(dialog));
    std::filesystem::path chosen;
    if (result == GTK_RESPONSE_ACCEPT) {
        gchar* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (filename) {
            chosen = filename;
            g_free(filename);
        }
    }
    g_object_unref(dialog);
    return chosen;
}

std::string FileDialog::chooseDirectory(const std::string& current, GtkWindow* parent) {
    GtkFileChooserNative* dialog = gtk_file_chooser_native_new(
        "Choose Folder", parent, GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "_Select", "_Cancel");
    if (!current.empty()) {
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), current.c_str());
    }
    gint result = gtk_native_dialog_run(GTK_NATIVE_DIALOG(dialog));
    std::string chosen;
    if (result == GTK_RESPONSE_ACCEPT) {
        gchar* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (filename) {
            chosen = filename;
            g_free(filename);
        }
    }
    g_object_unref(dialog);
    return chosen;
}

} // namespace sc::platform
