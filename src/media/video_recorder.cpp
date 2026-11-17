#include "screencapture/media/video_recorder.h"
#include "screencapture/platform/x11_display.h"
#include "screencapture/platform/screen_capturer.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <signal.h>
#include <string>
#include <unistd.h>

namespace sc::media {

namespace {

std::string resolveDisplayName() {
    const char* envDisp = std::getenv("DISPLAY");
    if (envDisp && *envDisp) return std::string(envDisp);
    return ":0";
}

// Full stdout of a shell command; empty when the command is unavailable.
std::string readAllOutput(const std::string& cmd) {
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return {};
    std::string out;
    char buf[256];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), p)) > 0) out.append(buf, n);
    pclose(p);
    return out;
}

bool binaryAvailable(const std::string& name) {
    return !readAllOutput("command -v " + name + " 2>/dev/null").empty();
}

// Value of "Default Sink:" / "Default Source:" from `pactl info` (works on
// PulseAudio 13.x and PipeWire's pipewire-pulse alike).
std::string pactlDefault(const std::string& key) {
    std::string info = readAllOutput("pactl info 2>/dev/null");
    std::string prefix = key + ": ";
    std::size_t pos = info.find(prefix);
    if (pos == std::string::npos) return {};
    std::size_t begin = pos + prefix.size();
    std::size_t end = info.find('\n', begin);
    std::string val = info.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
    while (!val.empty() && (val.back() == '\r' || val.back() == ' ')) val.pop_back();
    return val;
}

bool pulseSourceExists(const std::string& name) {
    if (name.empty()) return false;
    std::string list = readAllOutput("pactl list short sources 2>/dev/null");
    return list.find("\t" + name + "\t") != std::string::npos;
}

// Monitor source of the default sink (system audio); empty if unavailable.
std::string defaultSpeakerSource() {
    std::string mon = pactlDefault("Default Sink");
    if (mon.empty()) return {};
    mon += ".monitor";
    return pulseSourceExists(mon) ? mon : std::string();
}

// Default recording source (microphone); empty if unavailable.
std::string defaultMicSource() {
    std::string src = pactlDefault("Default Source");
    return pulseSourceExists(src) ? src : std::string();
}

// PipeWire drives the audio server here. Pulse-protocol monitor capture is
// silent through pipewire-pulse on some setups, so feeders use pw-cat (native
// PipeWire) when available and fall back to parecord on real PulseAudio.
bool pipeWireActive() {
    const char* rt = std::getenv("XDG_RUNTIME_DIR");
    if (!rt) return false;
    if (!std::filesystem::exists(std::filesystem::path(rt) / "pipewire-0")) return false;
    return binaryAvailable("pw-cat");
}

// Shell command writing its own pid file before exec-ing the feeder binary;
// the recorder kills that pid to end the feeder.
std::string feederCommand(const std::string& source, const std::string& file,
                          const std::string& pidFile) {
    if (pipeWireActive()) {
        return "echo $$ > \"" + pidFile + "\"; exec pw-cat -r \"" + file +
               "\" --target \"" + source + "\" --format s16 --rate 44100 --channels 2";
    }
    return "echo $$ > \"" + pidFile +
           "\"; exec parecord --file-format=wav --format=s16le --rate=44100 --channels=2 -d \"" +
           source + "\" \"" + file + "\"";
}

// Runs a command to completion; output is drained (recorders stay quiet).
void runAndDrain(const std::string& cmd) {
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return;
    char buf[256];
    while (fread(buf, 1, sizeof(buf), p) > 0) {}
    pclose(p);
}

} // namespace

VideoRecorder::VideoRecorder() = default;
VideoRecorder::~VideoRecorder() { cancel(); }

bool VideoRecorder::ffmpegAvailable() {
    static const bool available = [] {
        FILE* p = popen("command -v ffmpeg 2>/dev/null", "r");
        if (!p) return false;
        char buf[8] = {};
        bool ok = fgets(buf, sizeof(buf), p) != nullptr && buf[0] != '\0';
        pclose(p);
        return ok;
    }();
    return available;
}

bool VideoRecorder::startMp4(const core::RectI& region, int fps, bool useSpeaker, bool useMic) {
    if (active_) return false;
    if (!ffmpegAvailable()) return false;
    auto d = sc::platform::X11Display::get()->virtualDesktop();
    int rx = std::max(0, region.left - d.left);
    int ry = std::max(0, region.top - d.top);
    int w = region.width() & ~1;
    int h = region.height() & ~1;
    if (w < 2 || h < 2) return false;

    const std::string pidStr = std::to_string(getpid());
    output_ = std::filesystem::temp_directory_path() / ("sc_" + pidStr + "_temp.mp4");
    std::error_code ec;
    std::filesystem::remove(output_, ec);

    // Audio goes through feeder processes into WAV files and is mixed onto
    // the video after stop; without usable audio sources record video-only.
    if ((useSpeaker || useMic) && launchAudioFeeders(useSpeaker, useMic)) {
        tempPath_ = (std::filesystem::temp_directory_path() / ("sc_" + pidStr + "_temp_v.mp4")).string();
        std::filesystem::remove(tempPath_, ec);
    } else {
        tempPath_.clear();
    }

    std::string disp = resolveDisplayName();
    std::string cmd = "ffmpeg -y -loglevel error -f x11grab -framerate " + std::to_string(fps) +
        " -video_size " + std::to_string(w) + "x" + std::to_string(h) +
        " -i " + disp + "+" + std::to_string(rx) + "," + std::to_string(ry) +
        " -c:v libx264 -preset ultrafast -pix_fmt yuv420p \"" +
        (tempPath_.empty() ? output_.string() : tempPath_) + "\"";
    if (!launch(cmd)) {
        stopAudioFeeders();
        return false;
    }
    gifMode_ = false;
    return true;
}

bool VideoRecorder::startGif(const core::RectI& region, int fps) {
    if (active_) return false;
    if (!ffmpegAvailable()) return false;
    auto d = sc::platform::X11Display::get()->virtualDesktop();
    int rx = std::max(0, region.left - d.left);
    int ry = std::max(0, region.top - d.top);
    int w = region.width() & ~1;
    int h = region.height() & ~1;
    if (w < 2 || h < 2) return false;

    gifFps_ = std::clamp(fps, 8, 20);
    const std::string pidStr = std::to_string(getpid());
    output_ = std::filesystem::temp_directory_path() / ("sc_" + pidStr + "_temp.gif");
    tempPath_ = (std::filesystem::temp_directory_path() / ("sc_" + pidStr + "_temp_v.mp4")).string();
    std::error_code ec;
    std::filesystem::remove(output_, ec);
    std::filesystem::remove(tempPath_, ec);

    // Stage 1 records a near-lossless MP4 (yuv444p keeps colors for the GIF
    // palette); convertTempToGif() assembles the GIF after stop, since a live
    // palettegen graph outputs an empty file when ffmpeg is quit via 'q'.
    std::string disp = resolveDisplayName();
    std::string cmd = "ffmpeg -y -loglevel error -f x11grab -framerate " + std::to_string(gifFps_) +
        " -video_size " + std::to_string(w) + "x" + std::to_string(h) +
        " -i " + disp + "+" + std::to_string(rx) + "," + std::to_string(ry) +
        " -c:v libx264 -preset ultrafast -crf 16 -pix_fmt yuv444p \"" + tempPath_ + "\"";
    if (!launch(cmd)) return false;
    gifMode_ = true;
    return true;
}

void VideoRecorder::stop() {
    if (!active_) return;
    active_ = false;
    finishProcess();
    stopAudioFeeders();
    if (gifMode_) {
        convertTempToGif();
    } else if (!audioFeeders_.empty()) {
        muxAudio();
    }
}

void VideoRecorder::cancel() {
    if (!active_) return;
    active_ = false;
    finishProcess();
    stopAudioFeeders();
    std::error_code ec;
    if (!tempPath_.empty()) std::filesystem::remove(tempPath_, ec);
    if (!output_.empty()) std::filesystem::remove(output_, ec);
    for (const auto& f : audioFeeders_) {
        std::filesystem::remove(f.file, ec);
        std::filesystem::remove(f.pidFile, ec);
    }
    audioFeeders_.clear();
}

bool VideoRecorder::launch(const std::string& cmd) {
    pipe_ = popen(cmd.c_str(), "w");
    if (!pipe_) return false;
    active_ = true;
    return true;
}

void VideoRecorder::finishProcess() {
    if (!pipe_) return;
    fwrite("q\n", 1, 2, pipe_);
    fflush(pipe_);
    pclose(pipe_);
    pipe_ = nullptr;
}

bool VideoRecorder::outputValid() const {
    if (output_.empty()) return false;
    std::error_code ec;
    auto size = std::filesystem::file_size(output_, ec);
    return !ec && size > 0;
}

bool VideoRecorder::launchAudioFeeders(bool useSpeaker, bool useMic) {
    if (!pipeWireActive() && !binaryAvailable("parecord")) return false;
    audioFeeders_.clear();
    auto add = [this](const std::string& src) {
        if (src.empty() || audioFeeders_.size() >= 2) return;
        AudioFeeder f;
        f.source = src;
        auto idx = std::to_string(audioFeeders_.size());
        const std::string pidStr = std::to_string(getpid());
        f.file = (std::filesystem::temp_directory_path() / ("sc_" + pidStr + "_temp_a" + idx + ".wav")).string();
        f.pidFile = (std::filesystem::temp_directory_path() / ("sc_" + pidStr + "_temp_a" + idx + ".pid")).string();
        std::error_code ec;
        std::filesystem::remove(f.file, ec);
        std::filesystem::remove(f.pidFile, ec);
        f.pipe = popen(feederCommand(f.source, f.file, f.pidFile).c_str(), "r");
        if (!f.pipe) return;
        audioFeeders_.push_back(std::move(f));
    };
    if (useSpeaker) add(defaultSpeakerSource());
    if (useMic) add(defaultMicSource());
    return !audioFeeders_.empty();
}

void VideoRecorder::stopAudioFeeders() {
    for (auto& f : audioFeeders_) {
        if (!f.pipe) continue;
        // The pid file holds the feeder process (written before its exec).
        long pid = 0;
        std::ifstream in(f.pidFile);
        if (in) in >> pid;
        if (pid > 0) kill(static_cast<pid_t>(pid), SIGTERM);
        pclose(f.pipe);
        f.pipe = nullptr;
        std::error_code ec;
        std::filesystem::remove(f.pidFile, ec);
    }
}

void VideoRecorder::muxAudio() {
    std::error_code ec;
    if (tempPath_.empty() || !std::filesystem::exists(tempPath_, ec)) return;

    std::string cmd = "ffmpeg -y -loglevel error -i \"" + tempPath_ + "\"";
    for (const auto& f : audioFeeders_) cmd += " -i \"" + f.file + "\"";
    if (audioFeeders_.size() == 1) {
        cmd += " -map 0:v:0 -map 1:a:0 -c:v copy -c:a aac -b:a 128k -shortest";
    } else {
        // amix without the normalize option: ffmpeg 4.2 (Ubuntu 20.04) lacks it.
        cmd += " -filter_complex \"[1:a][2:a]amix=inputs=2:duration=longest[aout]\""
               " -map 0:v -map \"[aout]\" -c:v copy -c:a aac -b:a 128k -shortest";
    }
    cmd += " \"" + output_.string() + "\"";
    runAndDrain(cmd);

    if (!outputValid()) std::filesystem::remove(output_, ec);
    std::filesystem::remove(tempPath_, ec);
    for (const auto& f : audioFeeders_) std::filesystem::remove(f.file, ec);
    audioFeeders_.clear();
}

void VideoRecorder::convertTempToGif() {
    std::error_code ec;
    bool haveTemp = !tempPath_.empty() && std::filesystem::exists(tempPath_, ec) && !ec &&
                    std::filesystem::file_size(tempPath_, ec) > 0 && !ec;
    if (!haveTemp) {
        std::filesystem::remove(tempPath_, ec);
        return;
    }
    runAndDrain("ffmpeg -y -loglevel error -i \"" + tempPath_ + "\" -vf \"fps=" +
                std::to_string(gifFps_) +
                ",split[s0][s1];[s0]palettegen[p];[s1][p]paletteuse\" -loop 0 \"" +
                output_.string() + "\"");
    if (!outputValid()) std::filesystem::remove(output_, ec);
    std::filesystem::remove(tempPath_, ec);
}

} // namespace sc::media
