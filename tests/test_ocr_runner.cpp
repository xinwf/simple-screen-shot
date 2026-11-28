#include "screencapture/gfx/image_surface.h"
#include "screencapture/media/ocr_runner.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::filesystem::path testRoot;
std::filesystem::path testCache;

struct TempDir {
    std::filesystem::path path;
    std::filesystem::path bin;
    std::filesystem::path cache;

    TempDir() {
        std::string pattern = (testRoot / "case_XXXXXX").string();
        std::vector<char> buf(pattern.begin(), pattern.end());
        buf.push_back('\0');
        assert(mkdtemp(buf.data()));
        path = buf.data();
        bin = path / "bin";
        cache = testCache;
        std::filesystem::create_directories(bin);
        setenv("PATH", bin.c_str(), 1);
        setenv("OCR_TEST_LOG", (path / "args.log").c_str(), 1);
    }

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }

    void writeExecutable(const std::filesystem::path& file, const std::string& body) {
        std::ofstream out(file, std::ios::binary);
        out << "#!/bin/sh\n" << body;
        out.close();
        assert(out.good());
        std::filesystem::permissions(file, std::filesystem::perms::owner_exec,
                                     std::filesystem::perm_options::add);
    }

    size_t tempPngs() const {
        size_t count = 0;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(cache / "screencapture", ec)) {
            if (entry.path().extension() == ".png") ++count;
        }
        return count;
    }
};

std::shared_ptr<sc::gfx::ImageSurface> testImage() {
    return std::make_shared<sc::gfx::ImageSurface>(8u, 8u);
}

void testSuccessfulPluginAndCleanup() {
    TempDir dir;
    auto plugin = dir.path / "plugins with spaces" / "ocr plugin.sh";
    std::filesystem::create_directories(plugin.parent_path());
    dir.writeExecutable(plugin, "printf 'plugin OCR text\\n'\n");

    assert(sc::media::OcrRunner::available(plugin.string()));
    auto result = sc::media::OcrRunner::recognize(testImage(), plugin.string());
    assert(result.success);
    assert(result.text == "plugin OCR text\n");
    assert(dir.tempPngs() == 0);
}

void testFailedPluginIsNotReportedAsEmptySuccess() {
    TempDir dir;
    auto plugin = dir.path / "failed plugin.sh";
    dir.writeExecutable(plugin, "exit 7\n");

    auto result = sc::media::OcrRunner::recognize(testImage(), plugin.string());
    assert(!result.success);
    assert(result.text.empty());
    assert(dir.tempPngs() == 0);
}

void testEnglishOnlyTesseractFallback() {
    TempDir dir;
    dir.writeExecutable(dir.bin / "tesseract",
                        "if [ \"$1\" = \"--list-langs\" ]; then\n"
                        "  printf 'List of available languages (2):\\neng\\nosd\\n'\n"
                        "  exit 0\n"
                        "fi\n"
                        "printf '%s\\n' \"$@\" > \"$OCR_TEST_LOG\"\n"
                        "printf 'fallback OCR text\\n'\n");

    // The bundled plugin may be present while python3 is absent. OCR must use
    // tesseract rather than claiming the .py file alone is an available engine.
    auto missingPython = dir.path / "not executable.py";
    std::ofstream(missingPython) << "pass\n";
    assert(sc::media::OcrRunner::available(missingPython.string()));
    auto result = sc::media::OcrRunner::recognize(testImage(), missingPython.string());
    assert(result.success);
    assert(result.text == "fallback OCR text\n");

    std::ifstream log(dir.path / "args.log");
    std::string args((std::istreambuf_iterator<char>(log)), std::istreambuf_iterator<char>());
    assert(args.find("-l\neng\n") != std::string::npos);
    assert(dir.tempPngs() == 0);
}

void testNoEngineIsFailure() {
    TempDir dir;
    auto plugin = dir.path / "missing.py";
    assert(!sc::media::OcrRunner::available(plugin.string()));
    auto result = sc::media::OcrRunner::recognize(testImage(), plugin.string());
    assert(!result.success);
    assert(result.text.empty());
    assert(dir.tempPngs() == 0);
}

} // namespace

int main() {
    std::string pattern = (std::filesystem::temp_directory_path() / "sc_ocr_tests_XXXXXX").string();
    std::vector<char> buf(pattern.begin(), pattern.end());
    buf.push_back('\0');
    assert(mkdtemp(buf.data()));
    testRoot = buf.data();
    testCache = testRoot / "cache";
    // GLib caches the user cache directory, so all cases must share one value.
    setenv("XDG_CACHE_HOME", testCache.c_str(), 1);

    testSuccessfulPluginAndCleanup();
    testFailedPluginIsNotReportedAsEmptySuccess();
    testEnglishOnlyTesseractFallback();
    testNoEngineIsFailure();
    std::error_code ec;
    std::filesystem::remove_all(testRoot, ec);
    std::printf("OCR runner tests: all tests passed\n");
    return 0;
}
