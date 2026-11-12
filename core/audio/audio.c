/* audio.c - Silver Diamond Game Engine :: spatial audio implementation
 *
 * Software mixer + platform output backends. See audio.h for the API.
 *
 * Threading: with a real output device a mixer thread calls the same
 * sdge_audio_mix() core under a lock; parameter setters take the same lock,
 * so play/stop/move calls from the game thread are always safe.
 */
#include "audio.h"
#include "../assets/assets.h"
#include "../assets/mp3.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

/* ============================== platform ============================== */
#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>          /* waveOut - present even in ancient MinGW */

static CRITICAL_SECTION g_lock;
#define A_LOCK()   EnterCriticalSection(&g_lock)
#define A_UNLOCK() LeaveCriticalSection(&g_lock)

/* the number of waveOut buffers: more buffers = a bigger cushion against
 * the underruns (the audible clicks) when the thread is late */
#define A_WIN_BUFFERS 8

typedef struct {
    HWAVEOUT  hwo;
    WAVEHDR   hdrs[A_WIN_BUFFERS];
    short    *samples[A_WIN_BUFFERS];  /* A_WIN_BUFFERS x SDGE_AUDIO_BLOCK stereo s16 */
    HANDLE    done_event;      /* signaled from the waveOut callback */
    HANDLE    thread;
} a_device_win;

#else /* linux / wsl */

#ifndef SDGE_NO_ALSA
#include <alsa/asoundlib.h>
#include <pthread.h>
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
#define A_LOCK()   pthread_mutex_lock(&g_lock)
#define A_UNLOCK() pthread_unlock(&g_lock)
#else
#define A_LOCK()   ((void)0)
#define A_UNLOCK() ((void)0)
#endif

#endif

/* ============================== state ================================ */
struct sdge_audio_source {
    int                active;      /* slot in use */
    sdge_audio_buffer *buf;
    int                playing;
    int                loop;

    float              volume;      /* 0..1+ */
    float              pitch;       /* resample step, 1 = natural */
    int                spatial;
    float              pos[3];
    float              vel[3];
    float              ref_dist;
    float              max_dist;

    /* runtime (touched under lock only) */
    double             cursor;      /* play head, in buffer frames */
    float              lpf[2];      /* air-absorption filter state L/R */
};

static struct {
    int   inited;
    int   device_ok;
    float master;
    float pos[3];
    float vel[3];
    float fwd[3];
    float up[3];
    float right[3];
    sdge_audio_source sources[SDGE_AUDIO_MAX_SOURCES];

#if defined(_WIN32)
    a_device_win dev;
#else
#ifndef SDGE_NO_ALSA
    snd_pcm_t *pcm;
    pthread_t  thread;
#endif
#endif
    volatile int run;
} g_audio;

/* ============================ small math ============================== */
#define A_PI 3.14159265358979323846f

static float a_clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

/* A soft limiter: a hard clip of a loud mix distorts (the shrill buzz),
 * this keeps the peaks smooth instead. */
#if defined(_WIN32) || !defined(SDGE_NO_ALSA)
static float a_soft_clip(float v) {
    if (v > 0.8f || v < -0.8f) {
        float x = v < 0.0f ? -v : v;
        float y = 0.8f + (1.0f - 0.8f) * tanhf((x - 0.8f) / (1.0f - 0.8f));
        return v < 0.0f ? -y : y;
    }
    return v;
}
#endif

static void a_v3_sub(float *o, const float *a, const float *b) {
    o[0] = a[0] - b[0]; o[1] = a[1] - b[1]; o[2] = a[2] - b[2];
}
static float a_v3_dot(const float *a, const float *b) {
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}
static float a_v3_len(const float *a) { return sqrtf(a_v3_dot(a, a)); }
static void a_v3_norm(float *o, const float *a) {
    float l = a_v3_len(a);
    if (l > 1e-9f) { o[0]=a[0]/l; o[1]=a[1]/l; o[2]=a[2]/l; }
    else { o[0]=0; o[1]=0; o[2]=0; }
}

/* one-pole low-pass, `a` in [0..1]; state advances in place */
static float a_lpf(float x, float *state, float a) {
    *state += a * (x - *state);
    return *state;
}

/* ============================== mixer ================================ */

/* per-block spatial params for one voice */
typedef struct {
    float gain_l, gain_r;      /* after volume */
    float lpf_a;               /* 1 = passthrough */
    float step;                /* buffer frames per output frame */
} a_mix_params;

static void a_voice_params(sdge_audio_source *s, a_mix_params *p) {
    p->gain_l = p->gain_r = s->volume;
    p->lpf_a  = 1.0f;
    p->step   = s->pitch > 0.0f ? s->pitch : 1.0f;

    if (!s->spatial) return;

    float dir[3];
    a_v3_sub(dir, s->pos, g_audio.pos);
    float d = a_v3_len(dir);
    if (d < 1e-6f) d = 1e-6f;
    float dirn[3]; a_v3_norm(dirn, dir);

    /* inverse distance attenuation, clamped to [ref, max] (OpenAL-style) */
    float ref = s->ref_dist > 0.01f ? s->ref_dist : 0.01f;
    float dc  = a_clampf(d, ref, s->max_dist > ref ? s->max_dist : ref);
    float att = ref / (ref + (dc - ref));
    att = a_clampf(att, 0.0f, 1.0f);

    /* equal-power panning against the listener right axis */
    float pan  = a_clampf(a_v3_dot(dirn, g_audio.right), -1.0f, 1.0f);
    float ang = (pan + 1.0f) * (A_PI * 0.25f);      /* 0 .. pi/2 */
    p->gain_l = cosf(ang) * att * s->volume;
    p->gain_r = sinf(ang) * att * s->volume;

    /* Doppler: radial closing speed raises the pitch */
    float rel[3];
    a_v3_sub(rel, s->vel, g_audio.vel);
    float vrel = a_v3_dot(rel, dirn);               /* + = moving away */
    float c = 343.0f;
    float dop = c / a_clampf(c + vrel, c * 0.25f, c * 4.0f);
    p->step = a_clampf(p->step * dop, 0.05f, 20.0f);

    /* air absorption: brightness falls from 20 kHz (at ref) to 700 Hz (max) */
    float span = s->max_dist > ref ? s->max_dist - ref : 1.0f;
    float k = a_clampf((d - ref) / span, 0.0f, 1.0f);
    float cutoff = 20000.0f - k * (20000.0f - 700.0f);
    p->lpf_a = 1.0f - expf(-2.0f * A_PI * cutoff / (float)SDGE_AUDIO_SAMPLE_RATE);
    if (p->lpf_a > 1.0f) p->lpf_a = 1.0f;
    if (p->lpf_a < 0.0f) p->lpf_a = 0.0f;
}

static void a_mix_voice(sdge_audio_source *s, float *out, int frames) {
    const sdge_audio_buffer *b = s->buf;
    if (!b || !b->samples || b->frame_count <= 0) { s->playing = 0; return; }

    a_mix_params p;
    a_voice_params(s, &p);

    const float *data = b->samples;
    int ch = b->channels >= 2 ? 2 : 1;
    int n = b->frame_count;
    double pos = s->cursor;
    int spatial_stereo = (s->spatial && ch == 2);   /* downmix to mono */

    for (int i = 0; i < frames; ++i) {
        if (pos >= (double)n) {
            if (s->loop) {
                pos -= floor(pos / (double)n) * (double)n;
            } else {
                s->playing = 0;
                s->cursor = 0.0;
                return;
            }
        }
        int i0 = (int)pos;
        int i1 = (i0 + 1 < n) ? i0 + 1 : (s->loop ? 0 : i0);
        float fr = (float)(pos - (double)i0);

        if (ch == 1) {
            float x = data[i0] + (data[i1] - data[i0]) * fr;
            if (s->spatial) x = a_lpf(x, &s->lpf[0], p.lpf_a);
            out[2 * i    ] += x * p.gain_l;
            out[2 * i + 1] += x * p.gain_r;
        } else {
            float xl = data[i0 * 2    ] + (data[i1 * 2    ] - data[i0 * 2    ]) * fr;
            float xr = data[i0 * 2 + 1] + (data[i1 * 2 + 1] - data[i0 * 2 + 1]) * fr;
            if (spatial_stereo) {
                float m = (xl + xr) * 0.5f;
                m = a_lpf(m, &s->lpf[0], p.lpf_a);
                out[2 * i    ] += m * p.gain_l;
                out[2 * i + 1] += m * p.gain_r;
            } else {
                out[2 * i    ] += a_lpf(xl, &s->lpf[0], p.lpf_a) * p.gain_l;
                out[2 * i + 1] += a_lpf(xr, &s->lpf[1], p.lpf_a) * p.gain_r;
            }
        }
        pos += p.step;
    }
    s->cursor = pos;
}

/* the mixer core; also called by the device thread */
SDGE_API void sdge_audio_mix(float *stereo_out, int frames) {
    if (!g_audio.inited || !stereo_out || frames <= 0) return;

    while (frames > 0) {
        int block = frames > SDGE_AUDIO_BLOCK ? SDGE_AUDIO_BLOCK : frames;
        A_LOCK();
        memset(stereo_out, 0, sizeof(float) * (size_t)block * 2u);
        for (int i = 0; i < SDGE_AUDIO_MAX_SOURCES; ++i) {
            sdge_audio_source *s = &g_audio.sources[i];
            if (s->active && s->playing) a_mix_voice(s, stereo_out, block);
        }
        A_UNLOCK();
        for (int i = 0; i < block * 2; ++i) {
            float v = stereo_out[i] * g_audio.master;
            stereo_out[i] = a_clampf(v, -1.0f, 1.0f);
        }
        stereo_out += block * 2;
        frames -= block;
    }
}

/* ========================= device backends =========================== */
#if defined(_WIN32)

static void CALLBACK a_wave_proc(HWAVEOUT hwo, UINT msg, DWORD_PTR inst,
                                 DWORD_PTR p1, DWORD_PTR p2) {
    (void)hwo; (void)inst; (void)p2;
    if (msg == WOM_DONE && p1) {
        WAVEHDR *h = (WAVEHDR *)p1;
        h->dwFlags |= WHDR_DONE;                    /* keep marked as free */
        SetEvent(g_audio.dev.done_event);
    }
}

static DWORD WINAPI a_dev_thread(LPVOID param) {
    (void)param;
    float mix[SDGE_AUDIO_BLOCK * 2];
    while (g_audio.run) {
        /* refill every completed header, then wait for the next completion */
        for (int i = 0; i < A_WIN_BUFFERS; ++i) {
            WAVEHDR *h = &g_audio.dev.hdrs[i];
            if (!(h->dwFlags & WHDR_DONE)) continue;      /* still playing */
            sdge_audio_mix(mix, SDGE_AUDIO_BLOCK);
            short *dst = g_audio.dev.samples[i];
            for (int k = 0; k < SDGE_AUDIO_BLOCK * 2; ++k)
                dst[k] = (short)(int)(a_soft_clip(mix[k]) * 32767.0f);
            h->dwBufferLength = SDGE_AUDIO_BLOCK * 2 * (DWORD)sizeof(short);
            waveOutWrite(g_audio.dev.hwo, h, sizeof(WAVEHDR));
        }
        WaitForSingleObject(g_audio.dev.done_event, 100);
    }
    return 0;
}

static int a_dev_open(void) {
    WAVEFORMATEX wfx;
    memset(&wfx, 0, sizeof(wfx));
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 2;
    wfx.nSamplesPerSec = SDGE_AUDIO_SAMPLE_RATE;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = 4;
    wfx.nAvgBytesPerSec = (DWORD)SDGE_AUDIO_SAMPLE_RATE * 4u;

    a_device_win *d = &g_audio.dev;
    if (waveOutOpen(&d->hwo, WAVE_MAPPER, &wfx,
                    (DWORD_PTR)a_wave_proc, 0, CALLBACK_FUNCTION) != MMSYSERR_NOERROR)
        return 0;
    d->done_event = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!d->done_event) { waveOutClose(d->hwo); d->hwo = NULL; return 0; }

    for (int i = 0; i < A_WIN_BUFFERS; ++i) {
        d->samples[i] = (short *)malloc(sizeof(short) * SDGE_AUDIO_BLOCK * 2u);
        if (!d->samples[i]) return 0;                        /* cleaned on close */
        memset(&d->hdrs[i], 0, sizeof(WAVEHDR));
        d->hdrs[i].lpData = (LPSTR)d->samples[i];
        d->hdrs[i].dwBufferLength = SDGE_AUDIO_BLOCK * 2 * (DWORD)sizeof(short);
        if (waveOutPrepareHeader(d->hwo, &d->hdrs[i], sizeof(WAVEHDR)) != MMSYSERR_NOERROR)
            return 0;
        d->hdrs[i].dwFlags |= WHDR_DONE;            /* mark free for filling */
    }
    g_audio.run = 1;
    d->thread = CreateThread(NULL, 0, a_dev_thread, NULL, 0, NULL);
    if (!d->thread) { g_audio.run = 0; return 0; }
    return 1;
}

static void a_dev_close(void) {
    a_device_win *d = &g_audio.dev;
    if (!d->hwo) return;
    g_audio.run = 0;
    if (d->done_event) SetEvent(d->done_event);
    if (d->thread) {
        WaitForSingleObject(d->thread, 2000);
        CloseHandle(d->thread);
        d->thread = NULL;
    }
    waveOutReset(d->hwo);
    for (int i = 0; i < A_WIN_BUFFERS; ++i) {
        if (d->hdrs[i].dwFlags & WHDR_PREPARED)
            waveOutUnprepareHeader(d->hwo, &d->hdrs[i], sizeof(WAVEHDR));
        free(d->samples[i]);
        d->samples[i] = NULL;
    }
    waveOutClose(d->hwo);
    d->hwo = NULL;
    if (d->done_event) { CloseHandle(d->done_event); d->done_event = NULL; }
}

#else /* ------------------------------------------------- linux / alsa */

#ifndef SDGE_NO_ALSA

static void *a_dev_thread(void *param) {
    (void)param;
    float mix[SDGE_AUDIO_BLOCK * 2];
    short pcm[SDGE_AUDIO_BLOCK * 2];
    while (g_audio.run) {
        sdge_audio_mix(mix, SDGE_AUDIO_BLOCK);
        for (int k = 0; k < SDGE_AUDIO_BLOCK * 2; ++k)
            pcm[k] = (short)(int)(a_soft_clip(mix[k]) * 32767.0f);
        snd_pcm_sframes_t w = snd_pcm_writei(g_audio.pcm, pcm, SDGE_AUDIO_BLOCK);
        if (w == -EPIPE) {              /* underrun */
            snd_pcm_prepare(g_audio.pcm);
        } else if (w == -ESTRPIPE) {    /* suspended (e.g. system sleep) */
            while (snd_pcm_resume(g_audio.pcm) == -EAGAIN) snd_pcm_wait(g_audio.pcm, 100);
            snd_pcm_prepare(g_audio.pcm);
        } else if (w < 0) {
            break;                      /* device gone: stop the thread */
        }
    }
    return NULL;
}

static int a_dev_open(void) {
    if (snd_pcm_open(&g_audio.pcm, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0)
        return 0;
    if (snd_pcm_set_params(g_audio.pcm, SND_PCM_FORMAT_S16_LE,
                           SND_PCM_ACCESS_RW_INTERLEAVED, 2,
                           SDGE_AUDIO_SAMPLE_RATE, 1 /*soft resample*/,
                           20000 /*20 ms latency*/) < 0) {
        snd_pcm_close(g_audio.pcm);
        g_audio.pcm = NULL;
        return 0;
    }
    g_audio.run = 1;
    if (pthread_create(&g_audio.thread, NULL, a_dev_thread, NULL) != 0) {
        g_audio.run = 0;
        snd_pcm_close(g_audio.pcm);
        g_audio.pcm = NULL;
        return 0;
    }
    return 1;
}

static void a_dev_close(void) {
    if (!g_audio.pcm) return;
    g_audio.run = 0;
    pthread_join(g_audio.thread, NULL);
    snd_pcm_drain(g_audio.pcm);
    snd_pcm_close(g_audio.pcm);
    g_audio.pcm = NULL;
}

#else /* SDGE_NO_ALSA: silent device */

static int a_dev_open(void)  { return 0; }
static void a_dev_close(void) { }

#endif /* SDGE_NO_ALSA */
#endif /* platform */

/* =============================== API ================================= */
SDGE_API int sdge_audio_init(void) {
    if (g_audio.inited) return 0;
    memset(&g_audio, 0, sizeof(g_audio));
    g_audio.master = 1.0f;
    g_audio.fwd[2] = -1.0f;                     /* -Z forward, +Y up */
    g_audio.up[1] = 1.0f;
    g_audio.right[0] = 1.0f;

#if defined(_WIN32)
    InitializeCriticalSection(&g_lock);
#endif
    g_audio.inited = 1;
    g_audio.device_ok = a_dev_open();
    if (!g_audio.device_ok)
        fprintf(stderr, "audio: no output device, running silent\n");
    return 0;
}

SDGE_API void sdge_audio_terminate(void) {
    if (!g_audio.inited) return;
    a_dev_close();
    for (int i = 0; i < SDGE_AUDIO_MAX_SOURCES; ++i) {
        g_audio.sources[i].active = 0;
        g_audio.sources[i].playing = 0;
    }
#if defined(_WIN32)
    DeleteCriticalSection(&g_lock);
#endif
    memset(&g_audio, 0, sizeof(g_audio));
}

SDGE_API int sdge_audio_has_device(void) { return g_audio.device_ok; }
SDGE_API int sdge_audio_sample_rate(void) { return SDGE_AUDIO_SAMPLE_RATE; }

SDGE_API void sdge_audio_set_master_volume(float v) {
    A_LOCK();
    g_audio.master = v < 0.0f ? 0.0f : v;
    A_UNLOCK();
}

SDGE_API float sdge_audio_get_master_volume(void) { return g_audio.master; }

/* ------------------------------------------------------------ buffers */

/* Resamples interleaved float PCM to the mixer rate (linear) and wraps it
 * into a mixer buffer. Takes ownership of `samples` (freed or handed over
 * to the buffer), so the caller must not free it afterwards. */
static sdge_audio_buffer *a_buffer_from_pcm(float *samples, int frames,
                                             int ch, int rate) {
    if (!samples || frames <= 0 || ch <= 0) { free(samples); return NULL; }
    if (ch > 2) ch = 2;
    if (rate <= 0) rate = SDGE_AUDIO_SAMPLE_RATE;

    if (rate != SDGE_AUDIO_SAMPLE_RATE) {
        /* load-time linear resample to the mixer rate */
        int nf = (int)((double)frames * (double)SDGE_AUDIO_SAMPLE_RATE
                       / (double)rate + 0.5);
        float *out = (float *)malloc(sizeof(float) * (size_t)nf * (size_t)ch);
        if (!out) { free(samples); return NULL; }
        double step = (double)rate / (double)SDGE_AUDIO_SAMPLE_RATE;
        for (int i = 0; i < nf; ++i) {
            double pos = (double)i * step;
            int i0 = (int)pos;
            int i1 = (i0 + 1 < frames) ? i0 + 1 : i0;
            float fr = (float)(pos - (double)i0);
            for (int c = 0; c < ch; ++c) {
                float a = samples[(size_t)i0 * (size_t)ch + (size_t)c];
                float b = samples[(size_t)i1 * (size_t)ch + (size_t)c];
                out[(size_t)i * (size_t)ch + (size_t)c] = a + (b - a) * fr;
            }
        }
        free(samples);
        frames = nf;
        samples = out;
    }

    sdge_audio_buffer *b = (sdge_audio_buffer *)malloc(sizeof(*b));
    if (!b) { free(samples); return NULL; }
    b->channels = ch;
    b->sample_rate = SDGE_AUDIO_SAMPLE_RATE;
    b->frame_count = frames;
    b->samples = samples;
    return b;
}

SDGE_API sdge_audio_buffer *sdge_audio_load_wav(const char *path) {
    sdga_wav wav;
    if (sdga_load_wav(path, &wav) != 0) {
        fprintf(stderr, "audio: cannot load '%s'\n", path);
        return NULL;
    }
    int ch = wav.channels >= 2 ? 2 : 1;
    int rate = wav.sample_rate > 0 ? wav.sample_rate : SDGE_AUDIO_SAMPLE_RATE;
    return a_buffer_from_pcm(wav.samples, wav.frame_count, ch, rate);
}

SDGE_API sdge_audio_buffer *sdge_audio_load_mp3(const char *path) {
    sdga_mp3 mp3;
    if (sdga_load_mp3(path, &mp3) != 0) {
        fprintf(stderr, "audio: cannot decode mp3 '%s'\n", path);
        return NULL;
    }
    int ch = mp3.channels >= 2 ? 2 : 1;
    return a_buffer_from_pcm(mp3.samples, mp3.frame_count, ch, mp3.sample_rate);
}

/* case-insensitive extension check (portable: no _stricmp on Linux) */
static int a_has_ext(const char *path, const char *ext) {
    const char *dot = strrchr(path, '.');
    if (!dot) return 0;
    size_t n = strlen(ext);
    if (strlen(dot) != n) return 0;
    for (size_t i = 0; i < n; ++i) {
        char a = dot[i], b = ext[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return 0;
    }
    return 1;
}

SDGE_API sdge_audio_buffer *sdge_audio_load_file(const char *path) {
    if (!path) return NULL;
    if (a_has_ext(path, ".mp3")) return sdge_audio_load_mp3(path);
    if (a_has_ext(path, ".wav")) return sdge_audio_load_wav(path);
    /* unknown extension: try mp3 first (self-describing frames), then wav */
    sdge_audio_buffer *b = sdge_audio_load_mp3(path);
    if (b) return b;
    return sdge_audio_load_wav(path);
}

SDGE_API void sdge_audio_free_buffer(sdge_audio_buffer *b) {
    if (!b) return;
    /* detach live sources so no voice can reference freed memory */
    A_LOCK();
    for (int i = 0; i < SDGE_AUDIO_MAX_SOURCES; ++i)
        if (g_audio.sources[i].active && g_audio.sources[i].buf == b) {
            g_audio.sources[i].playing = 0;
            g_audio.sources[i].buf = NULL;
        }
    A_UNLOCK();
    free((void *)b->samples);
    free(b);
}

/* ------------------------------------------------------------ sources */
SDGE_API sdge_audio_source *sdge_audio_source_create(sdge_audio_buffer *buf) {
    if (!buf || buf->frame_count <= 0) return NULL;
    A_LOCK();
    sdge_audio_source *s = NULL;
    for (int i = 0; i < SDGE_AUDIO_MAX_SOURCES; ++i)
        if (!g_audio.sources[i].active) { s = &g_audio.sources[i]; break; }
    if (s) {
        memset(s, 0, sizeof(*s));
        s->active = 1;
        s->buf = buf;
        s->volume = 1.0f;
        s->pitch = 1.0f;
        s->spatial = 1;
        s->ref_dist = 1.0f;
        s->max_dist = 50.0f;
    }
    A_UNLOCK();
    return s;
}

SDGE_API void sdge_audio_source_destroy(sdge_audio_source *s) {
    if (!s) return;
    A_LOCK();
    s->active = 0;
    s->playing = 0;
    s->buf = NULL;
    A_UNLOCK();
}

SDGE_API void sdge_audio_source_play(sdge_audio_source *s) {
    if (!s) return;
    A_LOCK();
    if (s->active) s->playing = 1;
    A_UNLOCK();
}

SDGE_API void sdge_audio_source_stop(sdge_audio_source *s) {
    if (!s) return;
    A_LOCK();
    s->playing = 0;
    s->cursor = 0.0;
    s->lpf[0] = s->lpf[1] = 0.0f;
    A_UNLOCK();
}

SDGE_API void sdge_audio_source_pause(sdge_audio_source *s) {
    if (!s) return;
    A_LOCK();
    s->playing = 0;
    A_UNLOCK();
}

SDGE_API int sdge_audio_source_playing(const sdge_audio_source *s) {
    return s ? s->playing : 0;
}

SDGE_API void sdge_audio_source_set_loop(sdge_audio_source *s, int loop) {
    if (!s) return;
    A_LOCK(); s->loop = loop ? 1 : 0; A_UNLOCK();
}

SDGE_API void sdge_audio_source_set_volume(sdge_audio_source *s, float v) {
    if (!s) return;
    A_LOCK(); s->volume = v < 0.0f ? 0.0f : v; A_UNLOCK();
}

SDGE_API void sdge_audio_source_set_pitch(sdge_audio_source *s, float p) {
    if (!s) return;
    A_LOCK(); s->pitch = p > 0.0f ? p : 1.0f; A_UNLOCK();
}

SDGE_API void sdge_audio_source_set_spatial(sdge_audio_source *s, int on) {
    if (!s) return;
    A_LOCK(); s->spatial = on ? 1 : 0; A_UNLOCK();
}

SDGE_API void sdge_audio_source_set_position(sdge_audio_source *s,
                                              float x, float y, float z) {
    if (!s) return;
    A_LOCK(); s->pos[0]=x; s->pos[1]=y; s->pos[2]=z; A_UNLOCK();
}

SDGE_API void sdge_audio_source_set_velocity(sdge_audio_source *s,
                                              float x, float y, float z) {
    if (!s) return;
    A_LOCK(); s->vel[0]=x; s->vel[1]=y; s->vel[2]=z; A_UNLOCK();
}

SDGE_API void sdge_audio_source_set_distance(sdge_audio_source *s,
                                              float ref, float max) {
    if (!s) return;
    A_LOCK();
    s->ref_dist = ref > 0.01f ? ref : 0.01f;
    s->max_dist = max > s->ref_dist ? max : s->ref_dist;
    A_UNLOCK();
}

/* ----------------------------------------------------------- listener */
SDGE_API void sdge_audio_listener_position(float x, float y, float z) {
    A_LOCK(); g_audio.pos[0]=x; g_audio.pos[1]=y; g_audio.pos[2]=z; A_UNLOCK();
}

SDGE_API void sdge_audio_listener_orientation(float fx, float fy, float fz,
                                              float ux, float uy, float uz) {
    float f[3] = { fx, fy, fz }, u[3] = { ux, uy, uz };
    a_v3_norm(f, f);
    a_v3_norm(u, u);
    A_LOCK();
    g_audio.fwd[0]=f[0]; g_audio.fwd[1]=f[1]; g_audio.fwd[2]=f[2];
    g_audio.up[0]=u[0];  g_audio.up[1]=u[1];  g_audio.up[2]=u[2];
    /* right = cross(forward, up) */
    g_audio.right[0] = f[1]*u[2] - f[2]*u[1];
    g_audio.right[1] = f[2]*u[0] - f[0]*u[2];
    g_audio.right[2] = f[0]*u[1] - f[1]*u[0];
    a_v3_norm(g_audio.right, g_audio.right);
    A_UNLOCK();
}

SDGE_API void sdge_audio_listener_velocity(float x, float y, float z) {
    A_LOCK(); g_audio.vel[0]=x; g_audio.vel[1]=y; g_audio.vel[2]=z; A_UNLOCK();
}
