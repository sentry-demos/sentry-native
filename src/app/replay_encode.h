#pragma once

// C helper that turns a window of I420 frames into an H.264 mp4 on disk.
// Isolated in C translation units because the vendored single-header encoder
// (minih264e) and muxer (minimp4) both define private symbols that clash when
// compiled together, and neither header is C++-clean.

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct EmpowerReplayFrame {
    const unsigned char* yuv; // I420 planar, width * height * 3 / 2 bytes
    unsigned duration_ticks;  // display duration in 90 kHz ticks
} EmpowerReplayFrame;

// Encodes `count` frames (first frame becomes the keyframe) into an mp4 at
// `path`, overwriting it. Width/height must be multiples of 16. `qp` is the
// fixed H.264 quantizer (10 = near lossless .. 51 = worst; ~23 is crisp for
// UI content). Returns 0 on success.
int empower_replay_encode_mp4(const char* path, int width, int height, int qp,
                              const EmpowerReplayFrame* frames, int count);

#ifdef __cplusplus
}
#endif
