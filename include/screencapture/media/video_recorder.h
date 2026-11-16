#pragma once
#include "screencapture/core/types.h"
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

namespace sc::media {

// FFmpeg-based MP4 recorder (x11grab + optional pulse) and cgif-based GIF recorder.
class VideoRecorder {
public:
    VideoRecorder();
    ~VideoRecorder();

    bool startMp4(const core::RectI& region, int fps = 30,
                  bool useSpeaker = true, bool useMic = false);
    bool startGif(const core::RectI& region, int fps = 16);
    void stop();

    bool recording() const noexcept { return active_; }
    std::filesystem::path outputPath() const noexcept { return output_; }
    bool isGif() const noexcept { return gifMode_; }

private:
    FILE* pipe_{nullptr};
    std::filesystem::path output_;
    bool active_{false};
    bool gifMode_{false};
    std::string tempPath_;
};

} // namespace sc::media
