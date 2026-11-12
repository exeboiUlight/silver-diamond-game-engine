/* mp3.h - Silver Diamond Game Engine :: MP3 asset decoder
 *
 * Thin engine-side wrapper around minimp3 (public domain, vendored in
 * include/minimp3). Decodes a whole MPEG-1/2/2.5 Layer III file into
 * interleaved float32 [-1..1] - the same layout sdga_load_wav produces,
 * so both feed the mixer directly.
 *
 * Decoding happens once, at load time; the mixer never sees MP3.
 */
#ifndef SDGE_ASSETS_MP3_H
#define SDGE_ASSETS_MP3_H

#include "../sdge_def.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int    channels;      /* 1 or 2 */
    int    sample_rate;   /* Hz (source rate, e.g. 44100) */
    int    frame_count;   /* frames = samples / channels */
    float *samples;       /* interleaved, [-1..1], malloc'd */
} sdga_mp3;

/* decodes `path` into `out`; 0 on success, negative on error */
SDGE_API int  sdga_load_mp3(const char *path, sdga_mp3 *out);
SDGE_API void sdga_free_mp3(sdga_mp3 *m);

#ifdef __cplusplus
}
#endif
#endif /* SDGE_ASSETS_MP3_H */
