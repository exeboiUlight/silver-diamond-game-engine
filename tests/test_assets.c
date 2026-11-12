/* tests/test_assets.c - automated tests for the asset loaders and the
 * audio chain (MP3 decode -> mixer buffer -> resample -> spatial mix).
 *
 * Cross-platform (Windows / Linux), no audio device required: the mixer
 * can be driven manually via sdge_audio_mix().
 *
 * Usage: test_assets [path-to-mp3]
 * Exit code = number of failed checks.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "assets/assets.h"
#include "assets/gltf.h"
#include "assets/mp3.h"
#include "audio/audio.h"

/* ---------------- test harness ---------------- */
static int g_failed = 0, g_total = 0;
#define CHECK(cond, name) do { \
    g_total++; \
    if (cond) printf("  [PASS] %s\n", (name)); \
    else { g_failed++; printf("  [FAIL] %s  (%s:%d)\n", (name), __FILE__, __LINE__); } \
} while (0)

static double g_peak;
static double g_rms;

/* measures the level of an interleaved stereo block */
static void measure(const float *buf, int frames) {
    g_peak = 0.0;
    double sum = 0.0;
    long n = (long)frames * 2;
    for (long i = 0; i < n; ++i) {
        double v = fabs((double)buf[i]);
        if (v > g_peak) g_peak = v;
        sum += (double)buf[i] * (double)buf[i];
    }
    g_rms = n ? sqrt(sum / (double)n) : 0.0;
}

int main(int argc, char **argv) {
    const char *mp3_path = (argc > 1) ? argv[1] : "assets/Detective(2).mp3";

    printf("test_assets: mp3 = '%s'\n", mp3_path);

    /* ------------------------------------------------- raw file slurp */
    {
        unsigned char *data = NULL;
        size_t size = 0;
        int rc = sdga_read_file(mp3_path, &data, &size);
        CHECK(rc == 0 && data && size > 100000, "read_file: mp3 slurped");
        if (data) {
            CHECK(data[0] == 'I' && data[1] == 'D' && data[2] == '3',
                  "read_file: ID3v2 tag present");
            free(data);
        }
        CHECK(sdga_read_file("no/such/file.mp3", &data, &size) != 0,
              "read_file: missing file fails");
    }

    /* ------------------------------------------------------- MP3 decode */
    {
        sdga_mp3 m;
        int rc = sdga_load_mp3(mp3_path, &m);
        CHECK(rc == 0, "load_mp3: decodes");
        if (rc == 0) {
            printf("        ch=%d rate=%d frames=%d (%.2f s)\n",
                   m.channels, m.sample_rate, m.frame_count,
                   (double)m.frame_count / (double)m.sample_rate);
            CHECK(m.channels == 2, "load_mp3: stereo");
            CHECK(m.sample_rate == 44100, "load_mp3: 44100 Hz");
            CHECK(m.frame_count > 1200000 && m.frame_count < 1400000,
                  "load_mp3: ~28.9 s of frames");
            CHECK(m.samples != NULL, "load_mp3: samples allocated");

            double dur = (double)m.frame_count / (double)m.sample_rate;
            CHECK(dur > 28.0 && dur < 30.0, "load_mp3: duration 28..30 s");

            /* the signal must not be silence */
            double peak = 0.0, sum = 0.0;
            long n = (long)m.frame_count * m.channels;
            for (long i = 0; i < n; ++i) {
                double v = fabs((double)m.samples[i]);
                if (v > peak) peak = v;
                sum += (double)m.samples[i] * (double)m.samples[i];
            }
            double rms = sqrt(sum / (double)n);
            printf("        peak=%.4f rms=%.5f\n", peak, rms);
            CHECK(peak > 0.05 && peak <= 1.0, "load_mp3: peak in range");
            CHECK(rms > 0.005, "load_mp3: not silence (rms)");
            CHECK(rms < peak, "load_mp3: rms below peak");

            /* left and right channels must differ (real stereo mix) */
            double diff = 0.0;
            for (int f = 0; f < m.frame_count; f += 97) {
                double l = m.samples[f * 2];
                double r = m.samples[f * 2 + 1];
                diff += fabs(l - r);
            }
            CHECK(diff > 0.0, "load_mp3: stereo channels differ");

            /* values must stay inside [-1, 1] */
            int in_range = 1;
            for (long i = 0; i < n; i += 13)
                if (m.samples[i] < -1.0f || m.samples[i] > 1.0f) in_range = 0;
            CHECK(in_range, "load_mp3: samples within [-1, 1]");

            sdga_free_mp3(&m);
            CHECK(m.samples == NULL, "load_mp3: free clears the pointer");
        }

        /* a WAV file is not a valid MP3 -> must fail cleanly */
        sdga_mp3 bad;
        CHECK(sdga_load_mp3("tests/data/beep.wav", &bad) != 0,
              "load_mp3: rejects a wav file");
        CHECK(sdga_load_mp3("no/such/file.mp3", &bad) != 0,
              "load_mp3: missing file fails");
        CHECK(sdga_load_mp3(NULL, &bad) != 0, "load_mp3: NULL path fails");
    }

    /* -------------------------------------------------- WAV (regression) */
    {
        sdga_wav w;
        int rc = sdga_load_wav("tests/data/beep.wav", &w);
        CHECK(rc == 0, "load_wav: decodes");
        if (rc == 0) {
            printf("        ch=%d rate=%d frames=%d\n",
                   w.channels, w.sample_rate, w.frame_count);
            CHECK(w.channels >= 1 && w.channels <= 2, "load_wav: 1..2 channels");
            CHECK(w.sample_rate == 44100, "load_wav: 44100 Hz");
            CHECK(w.frame_count > 10000, "load_wav: frame count");
            CHECK(w.samples != NULL, "load_wav: samples allocated");
            sdga_free_wav(&w);
        }
        sdga_wav bad;
        CHECK(sdga_load_wav(mp3_path, &bad) != 0, "load_wav: rejects an mp3");
    }

    /* ------------------------------------------------------- glTF nodes */
    {
        sdge_gltf_model *m = sdge_load_gltf("Subjects/plane.glb");
        CHECK(m != NULL, "gltf: plane.glb loads");
        if (m) {
            CHECK(m->node_count >= 1, "gltf: plane has nodes");
            if (m->node_count >= 1) {
                const sdge_gltf_node *n = &m->nodes[0];
                /* a missing "scale" must keep the default (1,1,1) - a zero
                 * scale collapsed the whole model into a point */
                CHECK(n->scale[0] == 1.0f && n->scale[1] == 1.0f
                      && n->scale[2] == 1.0f, "gltf: node scale defaults to 1");
                CHECK(n->translation[0] == 0.0f && n->translation[1] == 0.0f
                      && n->translation[2] == 0.0f, "gltf: node translation is 0");
                /* the composed matrix must be the identity */
                int ident = 1;
                for (int c = 0; c < 4; ++c)
                    for (int r = 0; r < 4; ++r) {
                        float want = (c == r) ? 1.0f : 0.0f;
                        if (n->matrix[c * 4 + r] != want) ident = 0;
                    }
                CHECK(ident, "gltf: node matrix is the identity");
            }
            CHECK(m->mesh_count >= 1, "gltf: plane has a mesh");
            if (m->mesh_count >= 1 && m->meshes[0].prim_count >= 1) {
                const sdge_primitive *pr = &m->meshes[0].prims[0];
                CHECK(pr->positions.count > 0, "gltf: primitive has positions");
                CHECK(pr->normals.count > 0, "gltf: primitive has normals");
                CHECK(pr->indices.count > 0, "gltf: primitive has indices");
                /* the first vertex must not be the origin of a collapsed model */
                if (pr->positions.count > 0
                    && pr->positions.ctype == SDGE_GLTF_FLOAT
                    && pr->positions.comps == 3) {
                    size_t stride = pr->positions.stride
                                    ? (size_t)pr->positions.stride : 12u;
                    const float *f = (const float *)(pr->positions.data + stride);
                    CHECK(fabsf(f[0]) + fabsf(f[1]) + fabsf(f[2]) > 0.001f,
                          "gltf: first vertex is not collapsed to the origin");
                }
            }
            sdge_gltf_destroy(m);
        }
        CHECK(sdge_load_gltf("no/such/file.glb") == NULL,
              "gltf: missing file fails");
    }

    /* ------------------------------------------- engine audio buffers */
    {
        sdge_audio_init();

        sdge_audio_buffer *b = sdge_audio_load_file(mp3_path);
        CHECK(b != NULL, "audio_load_file: mp3 loaded");
        if (b) {
            printf("        ch=%d rate=%d frames=%d\n",
                   b->channels, b->sample_rate, b->frame_count);
            CHECK(b->channels == 2, "audio_load_file: stereo");
            CHECK(b->sample_rate == SDGE_AUDIO_SAMPLE_RATE,
                  "audio_load_file: resampled to the mixer rate");
            double dur = (double)b->frame_count / (double)b->sample_rate;
            CHECK(dur > 28.0 && dur < 30.0, "audio_load_file: duration kept");
            CHECK(b->samples != NULL, "audio_load_file: samples present");

            /* the resampled buffer must still carry signal */
            double peak = 0.0;
            long n = (long)b->frame_count * b->channels;
            for (long i = 0; i < n; i += 7) {
                double v = fabs((double)b->samples[i]);
                if (v > peak) peak = v;
            }
            CHECK(peak > 0.05, "audio_load_file: signal survived resampling");

            /* ---------------------------------------------------- mixer */
            sdge_audio_source *s = sdge_audio_source_create(b);
            CHECK(s != NULL, "audio_source: created");
            if (s) {
                sdge_audio_source_set_loop(s, 1);
                sdge_audio_source_set_volume(s, 1.0f);
                sdge_audio_source_play(s);
                CHECK(sdge_audio_source_playing(s), "audio_source: playing");

                int frames = SDGE_AUDIO_SAMPLE_RATE * 10;   /* 10 s */
                float *out = (float *)malloc(sizeof(float) * (size_t)frames * 2u);
                if (out) {
                    sdge_audio_mix(out, frames);
                    measure(out, frames);
                    printf("        mixed 10 s: peak=%.4f rms=%.5f\n",
                           g_peak, g_rms);
                    CHECK(g_peak > 0.005, "mixer: output is not silent");
                    CHECK(g_rms > 0.0005, "mixer: rms above noise floor");
                    free(out);
                }

                /* spatialization: a source to the right must be louder in
                 * the right channel */
                sdge_audio_source_stop(s);
                sdge_audio_listener_position(0.0f, 0.0f, 0.0f);
                sdge_audio_listener_orientation(0.0f, 0.0f, -1.0f,
                                                 0.0f, 1.0f, 0.0f);
                sdge_audio_source_set_spatial(s, 1);
                sdge_audio_source_set_position(s, 4.0f, 0.0f, 0.0f);
                sdge_audio_source_set_distance(s, 1.0f, 100.0f);
                sdge_audio_source_play(s);
                frames = SDGE_AUDIO_SAMPLE_RATE * 5;
                out = (float *)malloc(sizeof(float) * (size_t)frames * 2u);
                if (out) {
                    sdge_audio_mix(out, frames);
                    double l = 0.0, r = 0.0;
                    for (int i = 0; i < frames; ++i) {
                        l += fabs((double)out[i * 2]);
                        r += fabs((double)out[i * 2 + 1]);
                    }
                    printf("        spatial L=%.4f R=%.4f\n", l, r);
                    CHECK(r > l, "mixer: source at +X is louder on the right");
                    free(out);
                }

                sdge_audio_source_stop(s);
                CHECK(!sdge_audio_source_playing(s), "audio_source: stopped");
                sdge_audio_source_destroy(s);
            }
            sdge_audio_free_buffer(b);
        }

        sdge_audio_buffer *w = sdge_audio_load_wav("tests/data/beep.wav");
        CHECK(w != NULL, "audio_load_wav: engine buffer");
        if (w) {
            CHECK(w->sample_rate == SDGE_AUDIO_SAMPLE_RATE,
                  "audio_load_wav: resampled to the mixer rate");
            sdge_audio_free_buffer(w);
        }
        CHECK(sdge_audio_load_file("no/such/file.mp3") == NULL,
              "audio_load_file: missing file fails");

        sdge_audio_terminate();
    }

    printf("\ntest_assets: %d checks, %d failed\n", g_total, g_failed);
    return g_failed;
}
