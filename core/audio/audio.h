/* audio.h - Silver Diamond Game Engine :: spatial audio (positional sound)
 *
 * Self-contained software mixer, no external audio library:
 *   - fixed 48 kHz stereo float pipeline, 5 ms blocks;
 *   - up to SDGE_AUDIO_MAX_SOURCES voices from a preallocated pool
 *     (stable pointers, O(1) create/destroy, zero per-frame allocations);
 *   - spatialization per source: inverse distance attenuation, equal-power
 *     stereo panning against the listener frame, Doppler pitch shift,
 *     distance-driven air-absorption low-pass filter;
 *   - output backends (auto-selected, engine keeps running without one):
 *       Windows: waveOut (winmm)   Linux: ALSA   otherwise: silent device
 *     with a real device the mixer runs on its own thread; without one
 *     sdge_audio_mix() can still be called manually (tests / capture).
 *
 * Buffers are engine-owned; a buffer may back any number of sources.
 *
 *   sdge_audio_init();
 *   sdge_audio_buffer *b = sdge_audio_load_wav("step.wav");
 *   sdge_audio_source *s = sdge_audio_source_create(b);
 *   sdge_audio_source_set_spatial(s, 1);
 *   sdge_audio_source_set_position(s, 3, 0, -2);
 *   sdge_audio_source_play(s);
 *   ...
 *   sdge_audio_source_destroy(s);
 *   sdge_audio_free_buffer(b);
 *   sdge_audio_terminate();
 */
#ifndef SDGE_AUDIO_H
#define SDGE_AUDIO_H

#include "../sdge_def.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SDGE_AUDIO_MAX_SOURCES 64
#define SDGE_AUDIO_SAMPLE_RATE 48000
#define SDGE_AUDIO_BLOCK        240        /* frames per mix block (5 ms) */

/* ------------------------------------------------------------- device */
/* 0 on success. The mixer is usable even when no sound device exists
 * (WSL, headless CI): sdge_audio_has_device() tells which case it is. */
SDGE_API int  sdge_audio_init(void);
SDGE_API void sdge_audio_terminate(void);
SDGE_API int  sdge_audio_has_device(void);
SDGE_API int  sdge_audio_sample_rate(void);
SDGE_API void sdge_audio_set_master_volume(float v);       /* 0..1+ */
SDGE_API float sdge_audio_get_master_volume(void);

/* ------------------------------------------------------------ buffers */
typedef struct {
    int           channels;     /* 1 or 2 (stereo files may be downmixed) */
    int           sample_rate;  /* always SDGE_AUDIO_SAMPLE_RATE */
    int           frame_count;
    const float  *samples;      /* interleaved [-1..1], owned by buffer */
} sdge_audio_buffer;

/* decodes a WAV (PCM 8/16/24/32, float32, mono/stereo) and resamples it
 * to the mixer rate once, at load time; NULL on failure */
SDGE_API sdge_audio_buffer *sdge_audio_load_wav(const char *path);

/* decodes an MP3 (MPEG 1/2/2.5 Layer III) and resamples it to the mixer
 * rate once, at load time; NULL on failure */
SDGE_API sdge_audio_buffer *sdge_audio_load_mp3(const char *path);

/* picks the decoder by file extension (.mp3 / .wav); NULL on failure */
SDGE_API sdge_audio_buffer *sdge_audio_load_file(const char *path);

SDGE_API void sdge_audio_free_buffer(sdge_audio_buffer *b);

/* ------------------------------------------------------------ sources */
typedef struct sdge_audio_source sdge_audio_source;   /* opaque, pooled */

SDGE_API sdge_audio_source *sdge_audio_source_create(sdge_audio_buffer *buf);
SDGE_API void  sdge_audio_source_destroy(sdge_audio_source *s);

SDGE_API void  sdge_audio_source_play(sdge_audio_source *s);   /* from cursor */
SDGE_API void  sdge_audio_source_stop(sdge_audio_source *s);   /* to start */
SDGE_API void  sdge_audio_source_pause(sdge_audio_source *s);
SDGE_API int   sdge_audio_source_playing(const sdge_audio_source *s);

SDGE_API void  sdge_audio_source_set_loop(sdge_audio_source *s, int loop);
SDGE_API void  sdge_audio_source_set_volume(sdge_audio_source *s, float v);
SDGE_API void  sdge_audio_source_set_pitch(sdge_audio_source *s, float p); /* 1 = normal */

SDGE_API void  sdge_audio_source_set_spatial(sdge_audio_source *s, int on);
SDGE_API void  sdge_audio_source_set_position(sdge_audio_source *s,
                                               float x, float y, float z);
SDGE_API void  sdge_audio_source_set_velocity(sdge_audio_source *s,
                                               float x, float y, float z);
/* distance model: inverse, clamped between ref and max (like OpenAL) */
SDGE_API void  sdge_audio_source_set_distance(sdge_audio_source *s,
                                               float ref, float max);

/* ----------------------------------------------------------- listener */
SDGE_API void sdge_audio_listener_position(float x, float y, float z);
SDGE_API void sdge_audio_listener_orientation(float fx, float fy, float fz,
                                              float ux, float uy, float uz);
SDGE_API void sdge_audio_listener_velocity(float x, float y, float z);

/* ------------------------------------------------------- manual mixing */
/* mixes `frames` stereo frames into `stereo_out` (2*frames floats) and
 * advances all voices; runs the device thread's mixer without a device */
SDGE_API void sdge_audio_mix(float *stereo_out, int frames);

#ifdef __cplusplus
}
#endif
#endif /* SDGE_AUDIO_H */
