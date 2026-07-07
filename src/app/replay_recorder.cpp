#include "app/replay_recorder.h"

#include "app/replay_encode.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <system_error>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

namespace empower {
namespace {

// Replaces `to` with `from` in one step so the crash daemon never observes a
// partially written clip or sidecar.
bool rename_over(const std::string& from, const std::string& to) {
#if defined(_WIN32)
    return MoveFileExA(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
    return std::rename(from.c_str(), to.c_str()) == 0;
#endif
}

constexpr int kTimescale = 90000; // mp4 ticks per second

} // namespace

bool ReplayRecorder::init(const Config& config) {
    if (config.replays_dir.empty() || config.replay_id.size() != 32) {
        return false;
    }
    config_ = config;
    // Keep experiment-friendly knobs (env-overridable in the GUI) sane.
    config_.window_seconds = std::clamp(config_.window_seconds, 2.0f, 60.0f);
    config_.capture_fps = std::clamp(config_.capture_fps, 0.5f, 30.0f);
    config_.rotation_seconds = std::clamp(config_.rotation_seconds, 0.5f, 10.0f);
    config_.max_width = std::clamp(config_.max_width, 256, 4096);

    // Recreate the staging directory and drop clips from previous runs: the
    // SDK consumes staged files only on a crash, so a clean exit leaves them
    // behind and they would otherwise accumulate forever.
    std::error_code ec;
    std::filesystem::create_directories(config_.replays_dir, ec);
    for (const auto& entry :
         std::filesystem::directory_iterator(config_.replays_dir, ec)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("replay-", 0) == 0) {
            std::filesystem::remove(entry.path(), ec);
        }
    }

    const std::string base = config_.replays_dir + "/replay-" + config_.replay_id;
    mp4_path_ = base + ".mp4";
    sidecar_path_ = base + ".json";

    stop_ = false;
    rotator_ = std::thread(&ReplayRecorder::rotate_loop, this);
    active_ = true;
    return true;
}

void ReplayRecorder::shutdown() {
    if (!active_) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    rotator_.join();
    active_ = false;
}

bool ReplayRecorder::frame_due(double now_unix_sec) const {
    return active_
        && now_unix_sec - last_capture_sec_ >= 1.0 / config_.capture_fps;
}

void ReplayRecorder::submit_frame(const unsigned char* rgba, int width,
                                  int height, double now_unix_sec) {
    if (!active_ || width < 32 || height < 32) {
        return;
    }
    if (enc_width_ == 0) {
        // Lock encode dimensions to the first grab, scaled to fit max_width
        // and truncated to macroblock multiples (the encoder requires x16).
        const double scale
            = std::min(1.0, static_cast<double>(config_.max_width) / width);
        enc_width_ = std::max(32, static_cast<int>(width * scale) & ~15);
        enc_height_ = std::max(32, static_cast<int>(height * scale) & ~15);
    }
    last_capture_sec_ = now_unix_sec;

    // Box-downscale into packed RGB, flipping the bottom-up GL rows.
    const int ew = enc_width_, eh = enc_height_;
    std::vector<unsigned char> rgb(static_cast<size_t>(ew) * eh * 3);
    const int box = std::max(1, width / ew);
    for (int dy = 0; dy < eh; ++dy) {
        const int sy0 = dy * height / eh;
        for (int dx = 0; dx < ew; ++dx) {
            const int sx0 = dx * width / ew;
            unsigned r = 0, g = 0, b = 0, n = 0;
            for (int by = 0; by < box; ++by) {
                const int sy = std::min(sy0 + by, height - 1);
                const unsigned char* row
                    = rgba + static_cast<size_t>(height - 1 - sy) * width * 4;
                for (int bx = 0; bx < box; ++bx) {
                    const unsigned char* px
                        = row + std::min(sx0 + bx, width - 1) * 4;
                    r += px[0];
                    g += px[1];
                    b += px[2];
                    ++n;
                }
            }
            unsigned char* out = &rgb[(static_cast<size_t>(dy) * ew + dx) * 3];
            out[0] = static_cast<unsigned char>(r / n);
            out[1] = static_cast<unsigned char>(g / n);
            out[2] = static_cast<unsigned char>(b / n);
        }
    }

    // Pack as I420 (BT.601 limited range), chroma averaged over 2x2 blocks.
    auto yuv = std::make_shared<std::vector<unsigned char>>(
        static_cast<size_t>(ew) * eh * 3 / 2);
    unsigned char* yp = yuv->data();
    unsigned char* up = yp + static_cast<size_t>(ew) * eh;
    unsigned char* vp = up + static_cast<size_t>(ew) * eh / 4;
    for (int y = 0; y < eh; ++y) {
        for (int x = 0; x < ew; ++x) {
            const unsigned char* px = &rgb[(static_cast<size_t>(y) * ew + x) * 3];
            yp[static_cast<size_t>(y) * ew + x] = static_cast<unsigned char>(
                16 + ((66 * px[0] + 129 * px[1] + 25 * px[2]) >> 8));
        }
    }
    for (int y = 0; y < eh; y += 2) {
        for (int x = 0; x < ew; x += 2) {
            int r = 0, g = 0, b = 0;
            for (int i = 0; i < 4; ++i) {
                const unsigned char* px
                    = &rgb[((static_cast<size_t>(y) + i / 2) * ew + x + i % 2) * 3];
                r += px[0];
                g += px[1];
                b += px[2];
            }
            r /= 4, g /= 4, b /= 4;
            const size_t c = static_cast<size_t>(y / 2) * (ew / 2) + x / 2;
            up[c] = static_cast<unsigned char>(
                128 + ((-38 * r - 74 * g + 112 * b) >> 8));
            vp[c] = static_cast<unsigned char>(
                128 + ((112 * r - 94 * g - 18 * b) >> 8));
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    ring_.push_back({ std::move(yuv), now_unix_sec });
    while (ring_.size() > 1
        && now_unix_sec - ring_.front().timestamp_sec > config_.window_seconds) {
        ring_.pop_front();
    }
    dirty_ = true;
}

void ReplayRecorder::rotate_loop() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stop_) {
        wake_.wait_for(lock,
            std::chrono::milliseconds(
                static_cast<int>(config_.rotation_seconds * 1000)),
            [this] { return stop_; });
        if (!dirty_ || ring_.size() < 2) {
            continue;
        }
        dirty_ = false;
        std::deque<Frame> snapshot = ring_; // shared_ptr copies, cheap
        lock.unlock();
        write_snapshot(snapshot);
        lock.lock();
    }
    // Final rotation so the freshest window is staged even when the recorder
    // is torn down between ticks.
    if (dirty_ && ring_.size() >= 2) {
        write_snapshot(ring_);
    }
}

bool ReplayRecorder::write_snapshot(const std::deque<Frame>& frames) {
    // Real capture-time deltas keep playback wall-clock accurate; the bounds
    // only guard against degenerate gaps (offscreen renders, debugger stops).
    std::vector<EmpowerReplayFrame> views(frames.size());
    double duration_sec = 0;
    for (size_t i = 0; i < frames.size(); ++i) {
        double delta = i + 1 < frames.size()
            ? frames[i + 1].timestamp_sec - frames[i].timestamp_sec
            : 1.0 / config_.capture_fps;
        delta = std::clamp(delta, 1.0 / 60.0, 2.0);
        views[i] = { frames[i].yuv->data(),
            static_cast<unsigned>(delta * kTimescale) };
        duration_sec += delta;
    }

    const std::string tmp = mp4_path_ + ".tmp";
    if (empower_replay_encode_mp4(
            tmp.c_str(), enc_width_, enc_height_, config_.qp, views.data(),
            static_cast<int>(views.size()))
        || !rename_over(tmp, mp4_path_)) {
        std::remove(tmp.c_str());
        return false;
    }

    std::error_code ec;
    const auto size_bytes = std::filesystem::file_size(mp4_path_, ec);
    const double end_sec
        = frames.back().timestamp_sec + 1.0 / config_.capture_fps;
    return write_sidecar(end_sec - duration_sec, end_sec, duration_sec * 1000.0,
        static_cast<int>(frames.size()), ec ? 0 : static_cast<long long>(size_bytes));
}

bool ReplayRecorder::write_sidecar(double start_sec, double end_sec,
                                   double duration_ms, int frame_count,
                                   long long size_bytes) {
    // Field names and shape are sentry-native's embedder sidecar contract
    // (see sentry-native src/session_replay/sentry_session_replay.c).
    char json[512];
    std::snprintf(json, sizeof(json),
        "{\"replayId\":\"%s\",\"videoFilename\":\"replay-%s.mp4\","
        "\"replayType\":\"buffer\",\"segmentId\":0,"
        "\"startTimestampSec\":%.3f,\"endTimestampSec\":%.3f,"
        "\"width\":%d,\"height\":%d,\"durationMs\":%.0f,"
        "\"sizeBytes\":%lld,\"frameCount\":%d,\"frameRate\":%d}",
        config_.replay_id.c_str(), config_.replay_id.c_str(), start_sec,
        end_sec, enc_width_, enc_height_, duration_ms, size_bytes, frame_count,
        static_cast<int>(config_.capture_fps));

    const std::string tmp = sidecar_path_ + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) {
        return false;
    }
    const bool ok = std::fputs(json, f) >= 0;
    std::fclose(f);
    if (!ok || !rename_over(tmp, sidecar_path_)) {
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

} // namespace empower
