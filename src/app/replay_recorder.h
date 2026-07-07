#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <condition_variable>
#include <vector>

namespace empower {

// Rolling session-replay recorder: the embedder half of sentry-native's
// session-replay protocol (the same contract sentry-unreal implements).
//
// The GUI feeds it downsampled framebuffer grabs a few times a second; a
// background thread periodically re-encodes the retained window into
// `<database>/replays/replay-<id>.mp4` plus a JSON sidecar (temp file +
// atomic rename). The scope carries `contexts.replay.replay_id`, so when the
// app crashes the sentry-crash daemon picks the staged clip up, wraps it in a
// `replay_video` envelope enriched from the crash event, and sends it in the
// same session. On a clean exit the staged files are simply left behind and
// wiped on the next launch.
//
// Compared to the Unreal recorder this trades fidelity for simplicity: frames
// are kept as raw I420 in memory and the whole window is re-encoded per
// rotation (no fragment ring, no hardware encoder) — plenty for a dashboard
// captured at a few fps.
class ReplayRecorder {
public:
    struct Config {
        std::string replays_dir; // <database>/replays
        std::string replay_id;   // 32-char lowercase hex, no dashes
        float window_seconds = 15.0f;
        float capture_fps = 4.0f;      // advisory; callers pace capture
        float rotation_seconds = 2.0f; // staging refresh cadence
        // Quality/footprint trade-offs. The defaults keep the clip close to
        // the window's logical resolution and crisp enough to read UI text;
        // the raw I420 ring peaks around 90 MB (window_seconds * capture_fps
        // frames at max_width). Encoding costs ~4 ms/frame at 1280 wide.
        int max_width = 1280; // frames are downscaled to fit
        int qp = 23;          // fixed H.264 quantizer (10 best .. 51 worst)
    };

    ~ReplayRecorder() { shutdown(); }

    // Wipes stale clips from replays_dir and starts the rotation thread.
    bool init(const Config& config);
    void shutdown();

    bool active() const { return active_; }

    // True when enough time has passed since the last accepted frame that the
    // caller should grab and submit a new one.
    bool frame_due(double now_unix_sec) const;

    // Takes an RGBA framebuffer grab in OpenGL row order (bottom-up), scales
    // it down, converts to I420 and appends it to the rolling window. Frame
    // dimensions are locked in by the first submission. Called on the render
    // thread; costs a couple of milliseconds at dashboard sizes.
    void submit_frame(const unsigned char* rgba, int width, int height,
                      double now_unix_sec);

private:
    struct Frame {
        std::shared_ptr<std::vector<unsigned char>> yuv; // I420
        double timestamp_sec = 0; // unix time the frame was captured
    };

    void rotate_loop();
    bool write_snapshot(const std::deque<Frame>& frames);
    bool write_sidecar(double start_sec, double end_sec, double duration_ms,
                       int frame_count, long long size_bytes);

    Config config_;
    bool active_ = false;

    // Encode dimensions, fixed by the first frame (multiples of 16).
    int enc_width_ = 0;
    int enc_height_ = 0;
    double last_capture_sec_ = 0;

    std::mutex mutex_;
    std::deque<Frame> ring_;
    bool dirty_ = false;
    bool stop_ = false;
    std::condition_variable wake_;
    std::thread rotator_;

    std::string mp4_path_;
    std::string sidecar_path_;
};

} // namespace empower
