#include "screencapture/media/ocr_runner.h"
#include <glib.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <utility>
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

std::string spawnCapture(const std::vector<std::string>& args, gint* exitStatus,
                           std::string* errorOutput = nullptr);

std::string findProgram(const char* name) {
    gchar* path = g_find_program_in_path(name);
    if (!path) return {};
    std::string result(path);
    g_free(path);
    return result;
}

bool pluginRunnable(const std::string& pluginPath) {
    if (pluginPath.empty()) return false;
    std::error_code ec;
    if (!std::filesystem::exists(pluginPath, ec)) return false;
    // A bundled .py is not necessarily executable; invoke it with python3.
    // If Python is absent, let the caller use tesseract instead of reporting
    // that an unusable plugin makes OCR available.
    if (hasSuffix(pluginPath, ".py")) return !findProgram("python3").empty();
    return ::access(pluginPath.c_str(), X_OK) == 0;
}

std::string tesseractLanguage() {
    gint status = -1;
    std::string listing = spawnCapture({"tesseract", "--list-langs"}, &status);
    if (status != 0) return "chi_sim+eng";

    bool chiSim = false, eng = false;
    std::istringstream lines(listing);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        chiSim = chiSim || line == "chi_sim";
        eng = eng || line == "eng";
    }
    if (chiSim && eng) return "chi_sim+eng";
    if (chiSim) return "chi_sim";
    if (eng) return "eng";
    return "chi_sim+eng";
}

// Spawn to completion, returning stdout (empty when the command could not
// run at all). Stderr is captured only when the caller supplies errorOutput.
std::string spawnCapture(const std::vector<std::string>& args, gint* exitStatus,
                           std::string* errorOutput) {
    std::vector<char*> argv = argvView(args);
    gchar* out = nullptr;
    gchar* stderrOutput = nullptr;
    gint status = 0;
    GError* err = nullptr;
    GSpawnFlags flags = G_SPAWN_SEARCH_PATH;
    if (!errorOutput) flags = static_cast<GSpawnFlags>(flags | G_SPAWN_STDERR_TO_DEV_NULL);
    if (!g_spawn_sync(nullptr, argv.data(), nullptr, flags,
                      nullptr, nullptr, &out, errorOutput ? &stderrOutput : nullptr, &status, &err)) {
        if (errorOutput) *errorOutput = err ? err->message : "OCR process could not start";
        if (err) g_error_free(err);
        if (exitStatus) *exitStatus = -1;
        return {};
    }
    std::string result = out ? out : "";
    if (errorOutput) *errorOutput = stderrOutput ? stderrOutput : "";
    g_free(out);
    g_free(stderrOutput);
    if (exitStatus) *exitStatus = status;
    return result;
}

} // namespace

bool OcrRunner::available(const std::string& pluginPath) {
    if (pluginRunnable(pluginPath)) return true;
    // g_find_program_in_path also verifies the executable bit, which a bare
    // exists() check on PATH entries would miss.
    return !findProgram("tesseract").empty();
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

OcrResult OcrRunner::recognize(const std::shared_ptr<gfx::ImageSurface>& image,
                               const std::string& pluginPath) {
    OcrResult result;
    if (!image || !image->valid()) return result;
    // Private per-user cache dir instead of shared /tmp: fixed-name PNGs
    // there could be pre-seeded as symlinks by another user.
    std::error_code ec;
    auto dir = std::filesystem::path(g_get_user_cache_dir()) / "screencapture";
    std::filesystem::create_directories(dir, ec);
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    auto tmp = dir / ("sc_ocr_" + std::to_string(getpid()) + "_" + std::to_string(now) + ".png");
    if (!image->saveToPng(tmp)) return result;

    // Build argv: prefer a runnable plugin, then system tesseract. .py plugins
    // are launched through the system python3 interpreter. Paths never pass
    // through a shell, so quotes/spaces in paths are harmless.
    std::vector<std::string> args;
    if (pluginRunnable(pluginPath)) {
        if (hasSuffix(pluginPath, ".py")) args.push_back("python3");
        args.push_back(pluginPath);
        args.push_back("--image-path=" + tmp.string());
        args.push_back("--del-image=true");
    } else if (!findProgram("tesseract").empty()) {
        args = {"tesseract", tmp.string(), "stdout", "-l", tesseractLanguage()};
    }

    gint status = -1;
    std::string output;
    if (!args.empty()) output = spawnCapture(args, &status, &result.error);
    std::filesystem::remove(tmp, ec);
    if (status == 0) {
        result.text = std::move(output);
        result.success = true;
    }
    return result;
}

} // namespace sc::media
