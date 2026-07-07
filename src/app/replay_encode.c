#include "replay_encode.h"

#include <stdio.h>
#include <stdlib.h>

// Upstream's Apple-arm64 workaround references vtbl2q_u8, which does not
// exist; the equivalent AArch64 intrinsic is vqtbl2_u8 (same signature).
#if defined(__APPLE__) && defined(__aarch64__)
#define vtbl2q_u8 vqtbl2_u8
#endif
#define MINIH264_IMPLEMENTATION
#include "minih264e.h"
#include "minimp4.h"

static int mp4_write_cb(int64_t offset, const void* buffer, size_t size, void* token) {
    FILE* f = (FILE*)token;
#if defined(_WIN32)
    _fseeki64(f, offset, SEEK_SET);
#else
    fseeko(f, (off_t)offset, SEEK_SET);
#endif
    return fwrite(buffer, 1, size, f) != size;
}

int empower_replay_encode_mp4(const char* path, int width, int height, int qp,
                              const EmpowerReplayFrame* frames, int count) {
    if (!path || !frames || count <= 0 || width <= 0 || height <= 0
        || (width | height) & 15) {
        return -1;
    }
    if (qp < 10) {
        qp = 10;
    } else if (qp > 51) {
        qp = 51;
    }

    H264E_create_param_t create = { 0 };
    create.width = width;
    create.height = height;
    create.gop = count;           // the whole window is one GOP behind frame 0
    create.const_input_flag = 1;  // frames are shared with the live ring

    int sizeof_persist = 0, sizeof_scratch = 0;
    if (H264E_sizeof(&create, &sizeof_persist, &sizeof_scratch)) {
        return -1;
    }
    H264E_persist_t* enc = (H264E_persist_t*)malloc(sizeof_persist);
    H264E_scratch_t* scratch = (H264E_scratch_t*)malloc(sizeof_scratch);
    FILE* f = enc && scratch ? fopen(path, "wb") : NULL;
    MP4E_mux_t* mux = f ? MP4E_open(1 /*sequential*/, 0, f, mp4_write_cb) : NULL;

    int rc = -1;
    mp4_h26x_writer_t writer;
    if (mux && !H264E_init(enc, &create)
        && !mp4_h26x_write_init(&writer, mux, width, height, 0)) {
        rc = 0;
        for (int i = 0; i < count && rc == 0; ++i) {
            unsigned char* yuv = (unsigned char*)frames[i].yuv;
            H264E_io_yuv_t io = {
                { yuv, yuv + width * height, yuv + width * height * 5 / 4 },
                { width, width / 2, width / 2 }
            };
            H264E_run_param_t run = { 0 };
            run.frame_type
                = i == 0 ? H264E_FRAME_TYPE_KEY : H264E_FRAME_TYPE_P;
            run.encode_speed = H264E_SPEED_BALANCED;
            run.qp_min = qp; // fixed QP, no rate control
            run.qp_max = qp;

            unsigned char* coded = NULL;
            int coded_size = 0;
            if (H264E_encode(enc, scratch, &run, &io, &coded, &coded_size)
                || mp4_h26x_write_nal(
                    &writer, coded, coded_size, frames[i].duration_ticks)) {
                rc = -1;
            }
        }
        mp4_h26x_write_close(&writer);
    }

    if (mux) {
        MP4E_close(mux);
    }
    if (f) {
        fclose(f);
    }
    free(scratch);
    free(enc);
    return rc;
}
