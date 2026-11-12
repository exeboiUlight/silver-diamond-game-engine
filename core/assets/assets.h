/* assets.h - Silver Diamond Game Engine :: asset foundation
 *
 * - sdga_arena: chunked region allocator. Every allocation comes from a
 *   malloc'd block that NEVER moves, so pointers stay valid while the arena
 *   grows (unlike realloc-based arenas). A whole glTF model lives in ONE
 *   arena: load = O(#blocks) allocs, free = one call. No per-vertex malloc.
 * - sdga_read_file: whole-file slurp.
 * - sdga_load_wav: RIFF/WAVE decoder (PCM u8/s16/s24/s32, IEEE float32,
 *   WAVE_FORMAT_EXTENSIBLE) -> interleaved float32 [-1, 1].
 */
#ifndef SDGE_ASSETS_H
#define SDGE_ASSETS_H

#include "../sdge_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------- arena */
typedef struct sdga_block sdga_block;

typedef struct {
    sdga_block *head;      /* newest block first */
    size_t block_count;
    size_t bytes_used;    /* payload bytes handed out (without headers) */
} sdga_arena;

SDGE_API void  sdga_arena_init(sdga_arena *a, size_t first_block_size);
SDGE_API void *sdga_alloc(sdga_arena *a, size_t size);             /* 16B aligned */
SDGE_API void *sdga_alloc_aligned(sdga_arena *a, size_t size, size_t align);
SDGE_API char *sdga_strdup(sdga_arena *a, const char *s);
SDGE_API void  sdga_arena_destroy(sdga_arena *a);
SDGE_API size_t sdga_arena_used(const sdga_arena *a);

/* ------------------------------------------------------------- files */
/* reads the whole file; *out_data is malloc'd, caller frees(); 0 on success */
SDGE_API int sdga_read_file(const char *path, unsigned char **out_data,
                            size_t *out_size);

/* ------------------------------------------------------------- WAV */
typedef struct {
    int    channels;      /* 1 or 2 */
    int    sample_rate;   /* Hz */
    int    frame_count;   /* frames = samples / channels */
    float *samples;       /* interleaved, [-1..1], malloc'd */
} sdga_wav;

SDGE_API int sdga_load_wav(const char *path, sdga_wav *out); /* 0 on success */
SDGE_API void sdga_free_wav(sdga_wav *w);

#ifdef SDGE_ASSETS_IMPLEMENTATION
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct sdga_block {
    sdga_block *next;
    size_t used;          /* bytes used including this header */
    size_t cap;           /* total block size including this header */
    /* payload follows, aligned */
};

#define SDGA_BLOCK_HDR   sizeof(sdga_block)
#define SDGA_ALIGN       16

SDGE_API void sdga_arena_init(sdga_arena *a, size_t first_block_size) {
    a->head = NULL;
    a->block_count = 0;
    a->bytes_used = 0;
    if (first_block_size < 4096) first_block_size = 4096;
    /* pre-allocate the first block lazily on first alloc */
    sdga_block *b = (sdga_block *)malloc(first_block_size);
    if (!b) return;
    b->next = NULL;
    b->used = SDGA_BLOCK_HDR;
    b->cap = first_block_size;
    a->head = b;
    a->block_count = 1;
}

SDGE_API void *sdga_alloc_aligned(sdga_arena *a, size_t size, size_t align) {
    if (!a) return NULL;
    if (align == 0 || (align & (align - 1)) != 0) align = SDGA_ALIGN;
    if (!a->head) {                      /* first block failed to allocate earlier */
        sdga_block *b0 = (sdga_block *)malloc(4096);
        if (!b0) return NULL;
        b0->next = NULL; b0->used = SDGA_BLOCK_HDR; b0->cap = 4096;
        a->head = b0; a->block_count = 1;
    }
    sdga_block *b = a->head;
    size_t payload = (size_t)((unsigned char *)b + b->used);
    size_t pad = (align - (payload % align)) % align;
    size_t need = pad + size;
    if (b->used + need > b->cap) {
        size_t block = b->cap * 2;
        if (block < need + SDGA_BLOCK_HDR) block = need + SDGA_BLOCK_HDR;
        sdga_block *nb = (sdga_block *)malloc(block);
        if (!nb) return NULL;
        nb->next = b;
        nb->cap = block;
        nb->used = SDGA_BLOCK_HDR;
        a->head = nb;
        a->block_count++;
        b = nb;
        payload = (size_t)((unsigned char *)b + b->used);
        pad = (align - (payload % align)) % align;
        need = pad + size;
    }
    b->used += need;
    a->bytes_used += size;
    return (void *)((unsigned char *)b + b->used - size);
}

SDGE_API void *sdga_alloc(sdga_arena *a, size_t size) {
    return sdga_alloc_aligned(a, size, SDGA_ALIGN);
}

SDGE_API char *sdga_strdup(sdga_arena *a, const char *s) {
    size_t n = strlen(s) + 1;
    char *p = (char *)sdga_alloc(a, n);
    if (p) memcpy(p, s, n);
    return p;
}

SDGE_API void sdga_arena_destroy(sdga_arena *a) {
    if (!a) return;
    sdga_block *b = a->head;
    while (b) {
        sdga_block *n = b->next;
        free(b);
        b = n;
    }
    a->head = NULL;
    a->block_count = 0;
    a->bytes_used = 0;
}

SDGE_API size_t sdga_arena_used(const sdga_arena *a) {
    return a ? a->bytes_used : 0;
}

SDGE_API int sdga_read_file(const char *path, unsigned char **out_data,
                            size_t *out_size) {
    if (!path || !out_data || !out_size) return -1;
    *out_data = NULL;
    *out_size = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -2; }
    long n = ftell(f);
    if (n < 0) { fclose(f); return -2; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return -2; }
    unsigned char *buf = (unsigned char *)malloc((size_t)n ? (size_t)n : 1);
    if (!buf) { fclose(f); return -3; }
    if (n > 0 && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return -4;
    }
    fclose(f);
    *out_data = buf;
    *out_size = (size_t)n;
    return 0;
}

/* ------------------------------------------------------------ WAV */
static unsigned int sdga_u32le(const unsigned char *p) {
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8)
         | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}
static unsigned int sdga_u16le(const unsigned char *p) {
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}
static float sdga_f32le(const unsigned char *p) {
    float f;
    memcpy(&f, p, 4);   /* host assumed little-endian-compatible on x86 */
    return f;
}

SDGE_API int sdga_load_wav(const char *path, sdga_wav *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    unsigned char *data = NULL;
    size_t size = 0;
    int rc = sdga_read_file(path, &data, &size);
    if (rc != 0) return rc;

    if (size < 12 || memcmp(data, "RIFF", 4) != 0
        || memcmp(data + 8, "WAVE", 4) != 0) {
        free(data);
        return -10;                       /* not a RIFF/WAVE */
    }

    unsigned int fmt_tag = 0, channels = 0, rate = 0, bits = 0;
    const unsigned char *pcm = NULL;
    size_t pcm_size = 0;

    size_t pos = 12;
    while (pos + 8 <= size) {
        const unsigned char *id = data + pos;
        unsigned int csz = sdga_u32le(data + pos + 4);
        const unsigned char *body = data + pos + 8;
        size_t body_max = size - (pos + 8);
        size_t cbody = csz < body_max ? csz : body_max;

        if (memcmp(id, "fmt ", 4) == 0 && cbody >= 16) {
            fmt_tag  = sdga_u16le(body + 0);
            channels = sdga_u16le(body + 2);
            rate     = sdga_u32le(body + 4);
            bits     = sdga_u16le(body + 14);
            if (fmt_tag == 0xFFFE && cbody >= 40) {
                fmt_tag = sdga_u16le(body + 24);   /* SubFormat GUID first 2 bytes */
            }
        } else if (memcmp(id, "data", 4) == 0) {
            pcm = body;
            pcm_size = cbody;
        }
        pos += 8 + cbody + (cbody & 1);   /* chunks are word-aligned */
    }

    if (!pcm || !channels || !rate || !bits) { free(data); return -11; }
    if (fmt_tag != 1 && fmt_tag != 3)     { free(data); return -12; }  /* PCM / float */
    if (channels > 2)                     { free(data); return -13; }

    int bytes_per_sample = (int)(bits / 8);
    if (fmt_tag == 3 && bits != 32)       { free(data); return -14; }
    if (fmt_tag == 1 && bits != 8 && bits != 16 && bits != 24 && bits != 32) {
        free(data);
        return -14;
    }
    if (pcm_size % (size_t)(bytes_per_sample * channels) != 0) {
        /* tolerate a trailing partial frame by ignoring it */
    }
    int frames = (int)(pcm_size / (size_t)(bytes_per_sample * channels));
    float *samples = (float *)malloc(sizeof(float) * (size_t)(frames * (int)channels));
    if (!samples) { free(data); return -3; }

    const float s8inv  = 1.0f / 128.0f;
    const float s16inv = 1.0f / 32768.0f;
    const float s24inv = 1.0f / 8388608.0f;
    const float s32inv = 1.0f / 2147483648.0f;
    for (int i = 0; i < frames * (int)channels; ++i) {
        const unsigned char *p = pcm + (size_t)i * (size_t)bytes_per_sample;
        float v = 0.0f;
        switch (fmt_tag == 3 ? 32 : bits) {
        case 8:  v = ((float)p[0] - 128.0f) * s8inv;  break;
        case 16: v = (float)((short)(p[0] | (p[1] << 8))) * s16inv; break;
        case 24: {
            int v24 = p[0] | (p[1] << 8) | (p[2] << 16);
            if (v24 & 0x800000) v24 -= 0x1000000;
            v = (float)v24 * s24inv;
            break;
        }
        case 32:
            if (fmt_tag == 3) v = sdga_f32le(p);
            else v = (float)((int)sdga_u32le(p)) * s32inv;
            break;
        }
        samples[i] = v;
    }
    free(data);
    out->channels = (int)channels;
    out->sample_rate = (int)rate;
    out->frame_count = frames;
    out->samples = samples;
    return 0;
}

SDGE_API void sdga_free_wav(sdga_wav *w) {
    if (w && w->samples) {
        free(w->samples);
        w->samples = NULL;
    }
}

#endif /* SDGE_ASSETS_IMPLEMENTATION */

#ifdef __cplusplus
}
#endif
#endif /* SDGE_ASSETS_H */
