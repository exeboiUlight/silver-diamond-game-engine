/* mp3.c - Silver Diamond Game Engine :: MP3 asset decoder
 *
 * minimp3 (public domain) does the actual decoding; this TU walks the
 * frame list, converts int16 samples to float and keeps the whole
 * track in one malloc'd block (no per-frame allocations after growth).
 */
#include "mp3.h"
#include "assets.h"

#include <stdlib.h>
#include <string.h>

#include <minimp3/minimp3.h>

/* skip an ID3v2 tag ("ID3" + 10 byte header, syncsafe size) */
static size_t mp3_skip_id3v2(const unsigned char *d, size_t size) {
    if (size < 10 || d[0] != 'I' || d[1] != 'D' || d[2] != '3')
        return 0;
    if ((d[6] & 0x80) || (d[7] & 0x80) || (d[8] & 0x80) || (d[9] & 0x80))
        return 0;                       /* not syncsafe -> no tag */
    size_t tag = 10u
               + ((size_t)(d[6] & 0x7F) << 21)
               + ((size_t)(d[7] & 0x7F) << 14)
               + ((size_t)(d[8] & 0x7F) << 7)
               +  (size_t)(d[9] & 0x7F);
    return tag < size ? tag : 0;
}

SDGE_API int sdga_load_mp3(const char *path, sdga_mp3 *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));

    unsigned char *data = NULL;
    size_t size = 0;
    int rc = sdga_read_file(path, &data, &size);
    if (rc != 0) return rc;
    if (size < 16) { free(data); return -10; }

    size_t pos = mp3_skip_id3v2(data, size);

    mp3dec_t dec;
    mp3dec_init(&dec);

    int channels = 0;
    int rate = 0;
    int frames = 0;              /* frames written so far */
    int cap_frames = 0;
    float *samples = NULL;

    mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];

    while (pos + 4 < size) {
        mp3dec_frame_info_t info;
        memset(&info, 0, sizeof(info));

        int n = mp3dec_decode_frame(&dec, data + pos, (int)(size - pos),
                                    pcm, &info);
        int consumed = info.frame_bytes;
        if (consumed <= 0) break;              /* no more frames */
        pos += (size_t)consumed;

        if (n <= 0 || info.channels <= 0) continue;

        if (!channels) {                        /* first decoded frame */
            channels = info.channels > 2 ? 2 : info.channels;
            rate = info.hz;
        }
        if (info.channels != channels) {
            /* rare mid-file change: decode into mono and mix down/up */
            continue;
        }
        if (info.hz > 0) rate = info.hz;

        if (frames + n > cap_frames) {
            int ncap = cap_frames ? cap_frames * 2 : 4096;
            while (ncap < frames + n) ncap *= 2;
            float *ns = (float *)realloc(samples,
                                         sizeof(float) * (size_t)ncap
                                         * (size_t)channels);
            if (!ns) { free(samples); free(data); return -3; }
            samples = ns;
            cap_frames = ncap;
        }

        float *dst = samples + (size_t)frames * (size_t)channels;
        const float inv = 1.0f / 32768.0f;
        for (int i = 0; i < n * channels; ++i)
            dst[i] = (float)pcm[i] * inv;
        frames += n;
    }

    free(data);

    if (!samples || frames <= 0 || channels <= 0 || rate <= 0) {
        free(samples);
        return -11;                             /* no decodable frames */
    }

    out->channels = channels;
    out->sample_rate = rate;
    out->frame_count = frames;
    out->samples = samples;
    return 0;
}

SDGE_API void sdga_free_mp3(sdga_mp3 *m) {
    if (m && m->samples) {
        free(m->samples);
        m->samples = NULL;
    }
}
