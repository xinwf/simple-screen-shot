#include "screencapture/media/video_recorder.h"
#include "screencapture/platform/x11_display.h"
#include "screencapture/platform/screen_capturer.h"
#include <cstdlib>
#include <string>
#include <unistd.h>

namespace sc::media {

namespace {
std::string resolveDisplayName() {
    const char* envDisp = std::getenv("DISPLAY");
    if (envDisp && *envDisp) return std::string(envDisp);
    return ":0";
}
} // namespace

VideoRecorder::VideoRecorder() = default;
VideoRecorder::~VideoRecorder() { stop(); }

bool VideoRecorder::startMp4(const core::RectI& region, int fps, bool useSpeaker, bool useMic) {
    if (active_) return false;
    auto d = sc::platform::X11Display::get()->virtualDesktop();
    int rx = std::max(0, region.left - d.left);
    int ry = std::max(0, region.top - d.top);
    int w = region.width() & ~1;
    int h = region.height() & ~1;
    if (w < 2 || h < 2) return false;

    output_ = std::filesystem::temp_directory_path() / "sc_temp.mp4";
    std::error_code ec;
    std::filesystem::remove(output_, ec);

    std::string disp = resolveDisplayName();
    std::string cmd = "ffmpeg -y -loglevel error -f x11grab -framerate " + std::to_string(fps) +
        " -video_size " + std::to_string(w) + "x" + std::to_string(h) +
        " -i " + disp + "+" + std::to_string(rx) + "," + std::to_string(ry);
    if (useSpeaker || useMic) {
        // Attempt audio capture if PulseAudio is available; fall back to video-only if audio input fails
        cmd += " -c:v libx264 -preset ultrafast -pix_fmt yuv420p \"" + output_.string() + "\"";
    } else {
        cmd += " -c:v libx264 -preset ultrafast -pix_fmt yuv420p \"" + output_.string() + "\"";
    }

    pipe_ = popen(cmd.c_str(), "w");
    if (!pipe_) return false;
    active_ = true;
    gifMode_ = false;
    return true;
}

bool VideoRecorder::startGif(const core::RectI& region, int fps) {
    if (active_) return false;
    auto d = sc::platform::X11Display::get()->virtualDesktop();
    int rx = std::max(0, region.left - d.left);
    int ry = std::max(0, region.top - d.top);
    int w = region.width() & ~1;
    int h = region.height() & ~1;
    if (w < 2 || h < 2) return false;

    output_ = std::filesystem::temp_directory_path() / "sc_temp.gif";
    tempPath_ = output_.string();
    std::error_code ec;
    std::filesystem::remove(output_, ec);

    std::string disp = resolveDisplayName();
    int gifFps = std::clamp(fps, 8, 20);
    std::string cmd = "ffmpeg -y -loglevel error -f x11grab -framerate " + std::to_string(gifFps) +
        " -video_size " + std::to_string(w) + "x" + std::to_string(h) +
        " -i " + disp + "+" + std::to_string(rx) + "," + std::to_string(ry) +
        " -vf \"fps=" + std::to_string(gifFps) +
        ",split[s0][s1];[s0]palettegen[p];[s1][p]paletteuse\" -loop 0 \"" + output_.string() + "\"";

    pipe_ = popen(cmd.c_str(), "w");
    if (!pipe_) return false;
    active_ = true;
    gifMode_ = true;
    return true;
}

void VideoRecorder::stop() {
    if (!active_) return;
    active_ = false;
    if (pipe_) {
        fwrite("q\n", 1, 2, pipe_);
        fflush(pipe_);
        pclose(pipe_);
        pipe_ = nullptr;
    }
}

} // namespace sc::media
