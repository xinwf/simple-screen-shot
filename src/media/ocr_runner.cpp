#include "screencapture/media/ocr_runner.h"
#include <glib.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <unistd.h>

namespace sc::media {

namespace {

// .py plugins are launched through the system python3 interpreter.
bool hasSuffix(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::vector<char*> argvView(const std::vector<std::string>& args) {
    std::vector<char*> v;
    v.reserve(args.size() + 1);
    for (const auto& a : args) v.push_back(const_cast<char*>(a.c_str()));
    v.push_back(nullptr);
    return v;
}

// Spawn to completion, returning stdout (empty when the command could not
// run at all). Stderr is dropped like the old "2>/dev/null" did.
std::string spawnCapture(const std::vector<std::string>& args, gint* exitStatus) {
    std::vector<char*> argv = argvView(args);
    gchar* out = nullptr;
    gint status = 0;
    GError* err = nullptr;
    GSpawnFlags flags = static_cast<GSpawnFlags>(G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL);
    if (!g_spawn_sync(nullptr, argv.data(), nullptr, flags,
                      nullptr, nullptr, &out, nullptr, &status, &err)) {
        if (err) g_error_free(err);
        if (exitStatus) *exitStatus = -1;
        return {};
    }
    std::string result = out ? out : "";
    g_free(out);
    if (exitStatus) *exitStatus = status;
    return result;
}

} // namespace

bool OcrRunner::available(const std::string& pluginPath) {
    if (!pluginPath.empty() && std::filesystem::exists(pluginPath)) return true;
    // g_find_program_in_path also verifies the executable bit, which a bare
    // exists() check on PATH entries would miss.
    gchar* p = g_find_program_in_path("tesseract");
    bool ok = p != nullptr;
    g_free(p);
    return ok;
}

bool OcrRunner::rapidocrAvailable(const std::string& pluginPath) {
    if (pluginPath.empty() || !std::filesystem::exists(pluginPath)) return false;
    std::vector<std::string> args;
    if (hasSuffix(pluginPath, ".py")) args.push_back("python3");
    args.push_back(pluginPath);
    args.push_back("--check");
    gint status = -1;
    spawnCapture(args, &status);
    return status == 0; // exit code 0 = RapidOCR usable
}

std::string OcrRunner::recognize(const std::shared_ptr<gfx::ImageSurface>& image,
                                 const std::string& pluginPath) {
    if (!image || !image->valid()) return {};
    // Private per-user cache dir instead of shared /tmp: fixed-name PNGs
    // there could be pre-seeded as symlinks by another user.
    std::error_code ec;
    auto dir = std::filesystem::path(g_get_user_cache_dir()) / "screencapture";
    std::filesystem::create_directories(dir, ec);
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    auto tmp = dir / ("sc_ocr_" + std::to_string(getpid()) + "_" + std::to_string(now) + ".png");
    if (!image->saveToPng(tmp)) return {};

    // Build argv: prefer plugin, then system tesseract. .py plugins are
    // launched through the system python3 interpreter. Paths never pass
    // through a shell, so quotes/spaces in paths are harmless.
    std::vector<std::string> args;
    if (!pluginPath.empty() && std::filesystem::exists(pluginPath)) {
        if (hasSuffix(pluginPath, ".py")) args.push_back("python3");
        args.push_back(pluginPath);
        args.push_back("--image-path=" + tmp.string());
        args.push_back("--del-image=true");
    } else {
        args = {"tesseract", tmp.string(), "stdout", "-l", "chi_sim+eng"};
    }

    std::string output = spawnCapture(args, nullptr);
    std::filesystem::remove(tmp, ec);
    return output;
}

} // namespace sc::media
