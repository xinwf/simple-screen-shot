#pragma once
#include "screencapture/core/types.h"
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace sc::media {

// FFmpeg-based MP4 recorder (x11grab + optional audio feeder) and GIF
// recorder. Audio is captured by a separate feeder process into a WAV file —
// pulse-protocol monitor capture is silent through pipewire-pulse, so
// PipeWire systems use pw-cat while PulseAudio systems use parecord — and is
// mixed onto the video in a second pass at stop. GIF likewise records an
// intermediate MP4 first, because a live palettegen filter graph yields an
// empty file when ffmpeg is quit via the 'q' key (it does not drain the
// filter graph on an interrupted EOF).
class VideoRecorder {
public:
    VideoRecorder();
    ~VideoRecorder();

    // True when the ffmpeg binary is on PATH (cached after the first call).
    static bool ffmpegAvailable();

    bool startMp4(const core::RectI& region, int fps = 30,
                  bool useSpeaker = true, bool useMic = false);
    bool startGif(const core::RectI& region, int fps = 16);
    void stop();
    // Abort recording and discard any output (no GIF conversion or audio mux).
    void cancel();

    bool recording() const noexcept { return active_; }
    std::filesystem::path outputPath() const noexcept { return output_; }
    bool isGif() const noexcept { return gifMode_; }
    // True when output_ exists and is non-empty, i.e. the encode succeeded.
    bool outputValid() const;

private:
    struct AudioFeeder {
        FILE* pipe{nullptr};
        std::string source;  // pulse source name (sink monitor or mic)
        std::string file;    // WAV file being written
        std::string pidFile;
    };

    bool launch(const std::string& cmd);
    void finishProcess();
    bool launchAudioFeeders(bool useSpeaker, bool useMic);
    void stopAudioFeeders();
    void muxAudio();
    void convertTempToGif();

    FILE* pipe_{nullptr};
    std::filesystem::path output_;
    bool active_{false};
    bool gifMode_{false};
    int gifFps_{15};
    std::string tempPath_; // intermediate MP4 holding the raw frames
    std::vector<AudioFeeder> audioFeeders_;
};

} // namespace sc::media
