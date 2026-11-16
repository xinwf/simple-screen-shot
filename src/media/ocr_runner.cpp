#include "screencapture/media/ocr_runner.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace sc::media {

namespace {

// Search PATH for an executable, mirroring what popen()/sh would run.
bool existsOnPath(const std::string& name) {
    const char* path = std::getenv("PATH");
    if (!path) return false;
    std::error_code ec;
    const std::string dirs(path);
    size_t start = 0;
    while (start <= dirs.size()) {
        size_t end = dirs.find(':', start);
        std::string dir = dirs.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!dir.empty() && std::filesystem::exists(std::filesystem::path(dir) / name, ec)) {
            return true;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return false;
}

// .py plugins are launched through the system python3 interpreter.
bool hasSuffix(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace

bool OcrRunner::available(const std::string& pluginPath) {
    if (!pluginPath.empty() && std::filesystem::exists(pluginPath)) return true;
    return existsOnPath("tesseract");
}

bool OcrRunner::rapidocrAvailable(const std::string& pluginPath) {
    if (pluginPath.empty() || !std::filesystem::exists(pluginPath)) return false;
    std::string command =
        (hasSuffix(pluginPath, ".py") ? "python3 \"" : "\"") + pluginPath + "\" --check >/dev/null 2>&1";
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) return false;
    return pclose(pipe) == 0; // exit code 0 = RapidOCR usable
}

std::string OcrRunner::recognize(const std::shared_ptr<gfx::ImageSurface>& image,
                                 const std::string& pluginPath) {
    if (!image || !image->valid()) return {};
    auto tmp = std::filesystem::temp_directory_path() / "sc_ocr_tmp.png";
    if (!image->saveToPng(tmp)) return {};

    // Build command: prefer plugin, then system tesseract. .py plugins are
    // launched through the system python3 interpreter.
    std::string command;
    if (!pluginPath.empty() && std::filesystem::exists(pluginPath)) {
        std::string prefix = hasSuffix(pluginPath, ".py") ? "python3 \"" : "\"";
        command = prefix + pluginPath + "\" --image-path=\"" + tmp.string() + "\" --del-image=true";
    } else {
        command = "tesseract \"" + tmp.string() + "\" stdout -l chi_sim+eng 2>/dev/null";
    }

    std::string output;
    FILE* pipe = popen(command.c_str(), "r");
    if (pipe) {
        char buf[4096];
        while (fgets(buf, sizeof(buf), pipe)) output += buf;
        pclose(pipe);
    }
    if (pluginPath.empty()) {
        std::error_code ec;
        std::filesystem::remove(tmp, ec);
    }
    return output;
}

} // namespace sc::media
