#include "screencapture/media/video_recorder.h"
#include "screencapture/platform/x11_display.h"
#include "screencapture/platform/screen_capturer.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <signal.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace sc::media {

namespace {

std::string resolveDisplayName() {
    const char* envDisp = std::getenv("DISPLAY");
    if (envDisp && *envDisp) return std::string(envDisp);
    return ":0";
}

// NULL-terminated char* view of an argv vector. The strings must outlive the
// view; every call site here keeps them in the same scope.
std::vector<char*> argvView(const std::vector<std::string>& args) {
    std::vector<char*> v;
    v.reserve(args.size() + 1);
    for (const auto& a : args) v.push_back(const_cast<char*>(a.c_str()));
    v.push_back(nullptr);
    return v;
}

bool binaryAvailable(const char* name) {
    gchar* p = g_find_program_in_path(name);
    bool ok = p != nullptr;
    g_free(p);
    return ok;
}

// Spawn to completion, returning stdout (empty when the command could not
// run at all). Stderr is dropped.
std::string spawnCapture(const std::vector<std::string>& args) {
    std::vector<char*> argv = argvView(args);
    gchar* out = nullptr;
    gint status = 0;
    GError* err = nullptr;
    if (!g_spawn_sync(nullptr, argv.data(), nullptr, G_SPAWN_SEARCH_PATH,
                      nullptr, nullptr, &out, nullptr, &status, &err)) {
        if (err) g_error_free(err);
        return {};
    }
    std::string result = out ? out : "";
    g_free(out);
    return result;
}

// Spawn to completion; all output is dropped.
void spawnWait(const std::vector<std::string>& args) {
    std::vector<char*> argv = argvView(args);
    gint status = 0;
    GError* err = nullptr;
    if (!g_spawn_sync(nullptr, argv.data(), nullptr,
                      static_cast<GSpawnFlags>(G_SPAWN_SEARCH_PATH |
                          G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL),
                      nullptr, nullptr, nullptr, nullptr, &status, &err)) {
        if (err) g_error_free(err);
    }
}

// Value of "Default Sink:" / "Default Source:" from `pactl info` (works on
// PulseAudio 13.x and PipeWire's pipewire-pulse alike).
std::string pactlDefault(const std::string& key) {
    std::string info = spawnCapture({"pactl", "info"});
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
    std::string list = spawnCapture({"pactl", "list", "short", "sources"});
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

} // namespace

VideoRecorder::VideoRecorder() {
    // Private 0700 scratch dir. mkdtemp guarantees uniqueness even when
    // several exports overlap (e.g. a tray capture started while an earlier
    // GIF is still encoding).
    std::string tmpl = std::string(g_get_tmp_dir()) + "/sc_rec_XXXXXX";
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    const char* made = mkdtemp(buf.data());
    if (made) {
        workDir_ = made;
    } else {
        std::error_code ec;
        workDir_ = std::filesystem::path(g_get_user_cache_dir()) / "screencapture" /
                   ("rec_" + std::to_string(getpid()));
        std::filesystem::create_directories(workDir_, ec);
    }
}

VideoRecorder::~VideoRecorder() {
    cancel();
    std::error_code ec;
    if (!workDir_.empty()) std::filesystem::remove_all(workDir_, ec);
}

bool VideoRecorder::ffmpegAvailable() {
    static const bool available = [] { return binaryAvailable("ffmpeg"); }();
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

    output_ = workDir_ / "out.mp4";
    std::error_code ec;
    std::filesystem::remove(output_, ec);

    // Audio goes through feeder processes into WAV files and is mixed onto
    // the video after stop; without usable audio sources record video-only.
    if ((useSpeaker || useMic) && launchAudioFeeders(useSpeaker, useMic)) {
        tempPath_ = (workDir_ / "video.mp4").string();
        std::filesystem::remove(tempPath_, ec);
    } else {
        tempPath_.clear();
    }

    std::string disp = resolveDisplayName();
    std::vector<std::string> args = {
        "ffmpeg", "-y", "-loglevel", "error", "-f", "x11grab",
        "-framerate", std::to_string(fps),
        "-video_size", std::to_string(w) + "x" + std::to_string(h),
        "-i", disp + "+" + std::to_string(rx) + "," + std::to_string(ry),
        "-c:v", "libx264", "-preset", "ultrafast", "-pix_fmt", "yuv420p",
        tempPath_.empty() ? output_.string() : tempPath_
    };
    if (!launchFfmpeg(args)) {
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
    output_ = workDir_ / "out.gif";
    tempPath_ = (workDir_ / "video.mp4").string();
    std::error_code ec;
    std::filesystem::remove(output_, ec);
    std::filesystem::remove(tempPath_, ec);

    // Stage 1 records a near-lossless MP4 (yuv444p keeps colors for the GIF
    // palette); convertTempToGif() assembles the GIF after stop, since a live
    // palettegen graph outputs an empty file when ffmpeg is quit via 'q'.
    std::string disp = resolveDisplayName();
    std::vector<std::string> args = {
        "ffmpeg", "-y", "-loglevel", "error", "-f", "x11grab",
        "-framerate", std::to_string(gifFps_),
        "-video_size", std::to_string(w) + "x" + std::to_string(h),
        "-i", disp + "+" + std::to_string(rx) + "," + std::to_string(ry),
        "-c:v", "libx264", "-preset", "ultrafast", "-crf", "16",
        "-pix_fmt", "yuv444p", tempPath_
    };
    if (!launchFfmpeg(args)) return false;
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
    for (const auto& f : audioFeeders_) std::filesystem::remove(f.file, ec);
    audioFeeders_.clear();
}

bool VideoRecorder::launchFfmpeg(const std::vector<std::string>& args) {
    std::vector<char*> argv = argvView(args);
    gint in = -1;
    GPid pid = 0;
    GError* err = nullptr;
    // DO_NOT_REAP_CHILD: finishProcess() waitpids the child itself so the
    // mp4 trailer is guaranteed flushed before stop()/cancel() returns.
    if (!g_spawn_async_with_pipes(nullptr, argv.data(), nullptr,
            static_cast<GSpawnFlags>(G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD),
            nullptr, nullptr, &pid, &in, nullptr, nullptr, &err)) {
        if (err) g_error_free(err);
        return false;
    }
    ffmpegPid_ = pid;
    stdinFd_ = in;
    active_ = true;
    return true;
}

void VideoRecorder::finishProcess() {
    if (stdinFd_ >= 0) {
        ssize_t n = write(stdinFd_, "q\n", 2);
        (void)n;   // ffmpeg may already be gone; waitpid below settles it
        close(stdinFd_);
        stdinFd_ = -1;
    }
    if (ffmpegPid_ > 0) {
        int status = 0;
        waitpid(static_cast<pid_t>(ffmpegPid_), &status, 0);
        ffmpegPid_ = 0;
    }
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
        f.file = (workDir_ / ("audio" + idx + ".wav")).string();
        std::error_code ec;
        std::filesystem::remove(f.file, ec);
        // The recorder data (file, device name) is passed as positional
        // parameters "$1"/"$2", so a name containing quotes or $ can never
        // become shell syntax. exec keeps the spawned pid == the recorder
        // process itself, which stopAudioFeeders() kills and reaps directly
        // (no pid file a foreign process could pre-seed).
        std::vector<std::string> cmd;
        if (pipeWireActive()) {
            cmd = {"sh", "-c",
                   "exec pw-cat -r \"$1\" --target \"$2\" --format s16 --rate 44100 --channels 2",
                   "sc", f.file, src};
        } else {
            cmd = {"sh", "-c",
                   "exec parecord --file-format=wav --format=s16le --rate=44100 --channels=2 "
                   "-d \"$2\" \"$1\"",
                   "sc", f.file, src};
        }
        std::vector<char*> argv = argvView(cmd);
        GPid pid = 0;
        GError* err = nullptr;
        if (!g_spawn_async(nullptr, argv.data(), nullptr,
                static_cast<GSpawnFlags>(G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD |
                    G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL),
                nullptr, nullptr, &pid, &err)) {
            if (err) g_error_free(err);
            return;
        }
        f.pid = pid;
        audioFeeders_.push_back(std::move(f));
    };
    if (useSpeaker) add(defaultSpeakerSource());
    if (useMic) add(defaultMicSource());
    return !audioFeeders_.empty();
}

void VideoRecorder::stopAudioFeeders() {
    for (auto& f : audioFeeders_) {
        if (f.pid <= 0) continue;
        kill(static_cast<pid_t>(f.pid), SIGTERM);
        int status = 0;
        waitpid(static_cast<pid_t>(f.pid), &status, 0);
        f.pid = 0;
    }
}

void VideoRecorder::muxAudio() {
    std::error_code ec;
    if (tempPath_.empty() || !std::filesystem::exists(tempPath_, ec)) return;

    std::vector<std::string> args = {"ffmpeg", "-y", "-loglevel", "error", "-i", tempPath_};
    for (const auto& f : audioFeeders_) { args.push_back("-i"); args.push_back(f.file); }
    if (audioFeeders_.size() == 1) {
        for (const char* a : {"-map", "0:v:0", "-map", "1:a:0", "-c:v", "copy",
                              "-c:a", "aac", "-b:a", "128k", "-shortest"})
            args.push_back(a);
    } else {
        // amix without the normalize option: ffmpeg 4.2 (Ubuntu 20.04) lacks it.
        for (const char* a : {"-filter_complex", "[1:a][2:a]amix=inputs=2:duration=longest[aout]",
                              "-map", "0:v", "-map", "[aout]", "-c:v", "copy",
                              "-c:a", "aac", "-b:a", "128k", "-shortest"})
            args.push_back(a);
    }
    args.push_back(output_.string());
    spawnWait(args);

    if (!outputValid()) {
        // Fallback: smooth degradation to video-only if audio mixing failed,
        // ensuring the user's recorded footage is never lost.
        std::filesystem::remove(output_, ec);
        std::filesystem::rename(tempPath_, output_, ec);
        if (ec) {
            std::filesystem::copy_file(tempPath_, output_, std::filesystem::copy_options::overwrite_existing, ec);
        }
    }
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
    spawnWait({"ffmpeg", "-y", "-loglevel", "error", "-i", tempPath_,
               "-vf", "fps=" + std::to_string(gifFps_) +
                      ",split[s0][s1];[s0]palettegen[p];[s1][p]paletteuse",
               "-loop", "0", output_.string()});
    if (!outputValid()) std::filesystem::remove(output_, ec);
    std::filesystem::remove(tempPath_, ec);
}

} // namespace sc::media
