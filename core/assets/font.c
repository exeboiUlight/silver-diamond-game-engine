/* font.c - Silver Diamond Game Engine :: font loading (sfnt + TrueType)
 *
 * Parses a TTF/OTF container, resolves the character map, rasterizes the
 * glyph outlines with an anti-aliased scanline filler and packs the
 * bitmaps into an atlas image.
 *
 * The OpenType/CFF variant (the 'CFF ' table) lives in font_cff.c.
 */
#include "font.h"
#include "font_internal.h"
#include "assets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ==================================================================== */
/*                        the outline sink (pen)                        */
/* ==================================================================== */

void sdga_pen_init(sdga_pen *p, float scale) {
    memset(p, 0, sizeof(*p));
    p->scale = scale;
}

void sdga_pen_free(sdga_pen *p) {
    free(p->e);
    p->e = NULL;
    p->n = p->cap = 0;
}

static void pen_edge(sdga_pen *p, float x0, float y0, float x1, float y1) {
    if (p->n + 4 > p->cap) {
        int cap = p->cap ? p->cap * 2 : 512;
        while (cap < p->n + 4) cap *= 2;
        float *ne = (float *)realloc(p->e, sizeof(float) * (size_t)cap);
        if (!ne) return;
        p->e = ne;
        p->cap = cap;
    }
    p->e[p->n++] = x0; p->e[p->n++] = y0;
    p->e[p->n++] = x1; p->e[p->n++] = y1;
}

void sdga_pen_close(sdga_pen *p) {
    if (p->open) {
        if (p->cx != p->sx || p->cy != p->sy)
            pen_edge(p, p->cx, p->cy, p->sx, p->sy);
        p->open = 0;
    }
}

void sdga_pen_move(sdga_pen *p, float x, float y) {
    sdga_pen_close(p);
    p->cx = p->sx = x * p->scale;
    p->cy = p->sy = -y * p->scale;      /* the font's y axis points up */
    p->open = 1;
}

void sdga_pen_line(sdga_pen *p, float x, float y) {
    float px = x * p->scale, py = -y * p->scale;
    pen_edge(p, p->cx, p->cy, px, py);
    p->cx = px; p->cy = py;
}

/* how many segments a curve is flattened into */
static int curve_segments(float x0, float y0, float x1, float y1,
                          float x2, float y2, float x3, float y3) {
    float d = fabsf(x1 - x0) + fabsf(y1 - y0) +
              fabsf(x2 - x1) + fabsf(y2 - y1) +
              fabsf(x3 - x2) + fabsf(y3 - y2);
    int n = (int)(sqrtf(d) * 1.2f);
    if (n < 3) n = 3;
    if (n > 24) n = 24;
    return n;
}

void sdga_pen_quad(sdga_pen *p, float cx, float cy, float x, float y) {
    float x0 = p->cx / (p->scale ? p->scale : 1.0f);
    float y0 = -p->cy / (p->scale ? p->scale : 1.0f);
    /* the control point comes in font units */
    float px0 = x0 * p->scale, py0 = -y0 * p->scale;
    float pcx = cx * p->scale, pcy = -cy * p->scale;
    float px1 = x * p->scale, py1 = -y * p->scale;
    int n = curve_segments(px0, py0, pcx, pcy, pcx, pcy, px1, py1);
    for (int i = 1; i <= n; ++i) {
        float t = (float)i / (float)n;
        float mt = 1.0f - t;
        float bx = mt * mt * px0 + 2.0f * mt * t * pcx + t * t * px1;
        float by = mt * mt * py0 + 2.0f * mt * t * pcy + t * t * py1;
        pen_edge(p, p->cx, p->cy, bx, by);
        p->cx = bx; p->cy = by;
    }
}

void sdga_pen_cubic(sdga_pen *p, float c1x, float c1y,
                    float c2x, float c2y, float x, float y) {
    float px0 = p->cx, py0 = p->cy;
    float pc1x = c1x * p->scale, pc1y = -c1y * p->scale;
    float pc2x = c2x * p->scale, pc2y = -c2y * p->scale;
    float px1 = x * p->scale, py1 = -y * p->scale;
    int n = curve_segments(px0, py0, pc1x, pc1y, pc2x, pc2y, px1, py1);
    for (int i = 1; i <= n; ++i) {
        float t = (float)i / (float)n;
        float mt = 1.0f - t;
        float a = mt * mt * mt, b = 3.0f * mt * mt * t;
        float c = 3.0f * mt * t * t, d = t * t * t;
        float bx = a * px0 + b * pc1x + c * pc2x + d * px1;
        float by = a * py0 + b * pc1y + c * pc2y + d * py1;
        pen_edge(p, p->cx, p->cy, bx, by);
        p->cx = bx; p->cy = by;
    }
}

/* ==================================================================== */
/*                        the scanline rasterizer                       */
/* ==================================================================== */

#define FONT_SUBSAMPLES 5     /* vertical AA samples per pixel row */

static void span_add(float *cov, int w, float x0, float x1, float amount) {
    if (x1 <= x0) return;
    if (x0 < 0.0f) x0 = 0.0f;
    if (x1 > (float)w) x1 = (float)w;
    if (x1 <= x0) return;

    int ix0 = (int)floorf(x0);
    int ix1 = (int)floorf(x1);
    if (ix0 < 0) ix0 = 0;
    if (ix1 >= w) ix1 = w - 1;
    if (ix0 == ix1) {
        cov[ix0] += (x1 - x0) * amount;
        return;
    }
    cov[ix0] += ((float)(ix0 + 1) - x0) * amount;
    for (int x = ix0 + 1; x < ix1; ++x) cov[x] += amount;
    if (ix1 < w) cov[ix1] += (x1 - (float)ix1) * amount;
}

/* Rasterizes the edges of the pen into a coverage buffer (w * h floats).
 * `ox`/`oy` is the position of the bitmap's top-left corner in the pen
 * space (the baseline is at y = 0). */
static void rasterize(const sdga_pen *pen, float *cov, int w, int h,
                      float ox, float oy) {
    int edges = pen->n / 4;
    if (edges <= 0 || w <= 0 || h <= 0) return;

    float *xs = (float *)malloc(sizeof(float) * (size_t)edges);
    int   *dir = (int *)malloc(sizeof(int) * (size_t)edges);
    if (!xs || !dir) { free(xs); free(dir); return; }

    const float amount = 1.0f / (float)FONT_SUBSAMPLES;

    for (int y = 0; y < h; ++y) {
        float *row = cov + (size_t)y * (size_t)w;
        for (int s = 0; s < FONT_SUBSAMPLES; ++s) {
            float sy = oy + (float)y + ((float)s + 0.5f) / (float)FONT_SUBSAMPLES;
            int n = 0;
            for (int e = 0; e < edges; ++e) {
                float y0 = pen->e[e * 4 + 1];
                float y1 = pen->e[e * 4 + 3];
                if ((y0 <= sy) == (y1 <= sy)) continue;
                float x0 = pen->e[e * 4 + 0];
                float x1 = pen->e[e * 4 + 2];
                float t = (sy - y0) / (y1 - y0);
                xs[n] = x0 + t * (x1 - x0) - ox;
                dir[n] = (y1 > y0) ? 1 : -1;
                n++;
            }
            if (n < 2) continue;

            /* insertion sort (the crossing count is small) */
            for (int i = 1; i < n; ++i) {
                float kx = xs[i];
                int kd = dir[i];
                int j = i - 1;
                while (j >= 0 && xs[j] > kx) {
                    xs[j + 1] = xs[j];
                    dir[j + 1] = dir[j];
                    --j;
                }
                xs[j + 1] = kx;
                dir[j + 1] = kd;
            }

            /* the crossings are sorted by x, so the even-odd rule fills the
             * spans between the pairs - it does not depend on the winding
             * direction of the contours (the CFF fonts mix them) */
            for (int i = 0; i + 1 < n; i += 2)
                span_add(row, w, xs[i], xs[i + 1], amount);
        }
    }
    free(xs);
    free(dir);
}

/* ==================================================================== */
/*                          the sfnt container                          */
/* ==================================================================== */

static unsigned rd_u16(const unsigned char *p) {
    return (unsigned)((p[0] << 8) | p[1]);
}
static int rd_i16(const unsigned char *p) {
    return (int)(short)rd_u16(p);
}
static unsigned long rd_u32(const unsigned char *p) {
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) |
           ((unsigned long)p[2] << 8) | (unsigned long)p[3];
}

static int find_table(sdga_font *f, const char *tag,
                      const unsigned char **out, size_t *out_len) {
    if (f->size < 12) return 0;
    unsigned n = rd_u16(f->data + 4);
    if (n > 512) n = 512;
    for (unsigned i = 0; i < n; ++i) {
        size_t rec = 12 + (size_t)i * 16;
        if (rec + 16 > f->size) break;
        if (memcmp(f->data + rec, tag, 4) != 0) continue;
        unsigned long off = rd_u32(f->data + rec + 8);
        unsigned long len = rd_u32(f->data + rec + 12);
        if (off > f->size) return 0;
        if (off + len > f->size) len = (unsigned long)f->size - off;
        *out = f->data + off;
        *out_len = (size_t)len;
        return 1;
    }
    return 0;
}

/* ==================================================================== */
/*                              the cmap                                 */
/* ==================================================================== */

/* finds the best unicode subtable and returns its offset inside 'cmap' */
static const unsigned char *cmap_subtable(const sdga_font *f, int *format) {
    if (!f->cmap || f->cmap_len < 4) return NULL;
    unsigned n = rd_u16(f->cmap + 2);
    if (n > 64) n = 64;

    int best = -1, best_score = -1;
    const unsigned char *best_ptr = NULL;

    for (unsigned i = 0; i < n; ++i) {
        size_t rec = 4 + (size_t)i * 8;
        if (rec + 8 > f->cmap_len) break;
        unsigned plat = rd_u16(f->cmap + rec);
        unsigned enc  = rd_u16(f->cmap + rec + 2);
        unsigned long off = rd_u32(f->cmap + rec + 4);
        if (off + 4 > f->cmap_len) continue;

        const unsigned char *st = f->cmap + off;
        unsigned fmt = rd_u16(st);

        int score = -1;
        if (plat == 3 && enc == 10 && fmt == 12) score = 100;
        else if (plat == 0 && fmt == 12) score = 95;
        else if (plat == 3 && enc == 1 && fmt == 4) score = 90;
        else if (plat == 0 && fmt == 4) score = 85;
        else if (plat == 3 && enc == 0) score = 40;   /* the symbol cmap */
        else if (plat == 0) score = 30;

        if (score > best_score) {
            best_score = score;
            best = (int)fmt;
            best_ptr = st;
        }
    }
    if (!best_ptr) return NULL;
    *format = best;
    return best_ptr;
}

static int cmap_lookup(const sdga_font *f, unsigned cp) {
    int format = 0;
    const unsigned char *st = cmap_subtable(f, &format);
    if (!st) return 0;

    if (format == 0) {
        if (cp > 255) return 0;
        if ((size_t)cp + 1 >= f->cmap_len) return 0;
        return st[3 + cp];
    }
    if (format == 4) {
        if (cp > 0xFFFF) return 0;
        unsigned seg_x2 = rd_u16(st + 6);
        if (seg_x2 == 0) return 0;
        const unsigned char *end_codes = st + 14;
        const unsigned char *start_codes = end_codes + seg_x2 + 2;
        const unsigned char *id_deltas = start_codes + seg_x2;
        const unsigned char *id_ranges = id_deltas + seg_x2;
        for (unsigned i = 0; i < seg_x2; ++i) {
            unsigned end = rd_u16(end_codes + i * 2);
            if (cp > end) continue;
            unsigned start = rd_u16(start_codes + i * 2);
            if (cp < start) return 0;
            int delta = rd_i16(id_deltas + i * 2);
            unsigned ro = rd_u16(id_ranges + i * 2);
            if (ro == 0) return (int)((cp + (unsigned)delta) & 0xFFFF);
            const unsigned char *p = id_ranges + i * 2 + ro + (cp - start) * 2;
            if ((size_t)(p - f->cmap) + 2 > f->cmap_len) return 0;
            unsigned g = rd_u16(p);
            if (g == 0) return 0;
            return (int)((g + (unsigned)delta) & 0xFFFF);
        }
        return 0;
    }
    if (format == 6) {
        unsigned first = rd_u16(st + 6);
        unsigned count = rd_u16(st + 8);
        if (cp < first || cp >= first + count) return 0;
        return (int)rd_u16(st + 10 + (cp - first) * 2);
    }
    if (format == 12) {
        unsigned long ngroups = rd_u32(st + 12);
        if (ngroups > 100000) ngroups = 100000;
        unsigned long lo = 0, hi = ngroups;
        while (lo < hi) {
            unsigned long mid = (lo + hi) / 2;
            size_t g = 16 + (size_t)mid * 12;
            if (g + 12 > f->cmap_len) break;
            unsigned long sc = rd_u32(st + g);
            unsigned long ec = rd_u32(st + g + 4);
            if (cp < sc) hi = mid;
            else if (cp > ec) lo = mid + 1;
            else return (int)rd_u32(st + g + 8) + (int)(cp - sc);
        }
        return 0;
    }
    return 0;
}

/* ==================================================================== */
/*                          the TrueType outlines                       */
/* ==================================================================== */

static int glyf_range(const sdga_font *f, int gid, const unsigned char **out,
                      size_t *out_len) {
    if (gid < 0 || gid >= f->num_glyphs) return 0;
    if (!f->loca || !f->glyf) return 0;
    unsigned long a, b;
    if (f->loc_format == 0) {
        if ((size_t)(gid + 2) * 2 + 1 > f->loca_len) return 0;
        a = (unsigned long)rd_u16(f->loca + gid * 2) * 2;
        b = (unsigned long)rd_u16(f->loca + gid * 2 + 2) * 2;
    } else {
        if ((size_t)(gid + 2) * 4 + 3 > f->loca_len) return 0;
        a = rd_u32(f->loca + gid * 4);
        b = rd_u32(f->loca + gid * 4 + 4);
    }
    if (b <= a || b > f->glyf_len) return 0;
    *out = f->glyf + a;
    *out_len = (size_t)(b - a);
    return 1;
}

/* reads a simple glyph into the pen */
static int glyf_simple(sdga_font *f, const unsigned char *g, size_t len,
                       sdga_pen *pen) {
    (void)f;
    if (len < 10) return -1;
    int ncont = rd_i16(g);
    if (ncont <= 0) return -1;
    size_t p = 10;
    if (p + (size_t)ncont * 2 + 2 > len) return -1;

    int *ends = (int *)malloc(sizeof(int) * (size_t)ncont);
    if (!ends) return -1;
    for (int i = 0; i < ncont; ++i) {
        ends[i] = (int)rd_u16(g + p);
        p += 2;
    }
    int npts = ends[ncont - 1] + 1;
    if (npts <= 0 || npts > 10000) { free(ends); return -1; }

    unsigned short ins_len = rd_u16(g + p);
    p += 2 + ins_len;

    /* the flags */
    unsigned char *flags = (unsigned char *)malloc((size_t)npts);
    if (!flags) { free(ends); return -1; }
    for (int i = 0; i < npts;) {
        if (p >= len) { free(flags); free(ends); return -1; }
        unsigned char fl = g[p++];
        flags[i++] = fl;
        if (fl & 0x08) {                     /* the repeat count */
            if (p >= len) { free(flags); free(ends); return -1; }
            unsigned rep = g[p++];
            while (rep-- > 0 && i < npts) flags[i++] = fl;
        }
    }

    /* the coordinates */
    int *xs = (int *)malloc(sizeof(int) * (size_t)npts);
    int *ys = (int *)malloc(sizeof(int) * (size_t)npts);
    if (!xs || !ys) { free(xs); free(ys); free(flags); free(ends); return -1; }
    int v = 0;
    for (int i = 0; i < npts; ++i) {
        unsigned char fl = flags[i];
        if (fl & 0x02) {
            if (p >= len) goto fail;
            int d = g[p++];
            v += (fl & 0x10) ? d : -d;
        } else if (!(fl & 0x10)) {
            if (p + 2 > len) goto fail;
            v += rd_i16(g + p);
            p += 2;
        }
        xs[i] = v;
    }
    v = 0;
    for (int i = 0; i < npts; ++i) {
        unsigned char fl = flags[i];
        if (fl & 0x04) {
            if (p >= len) goto fail;
            int d = g[p++];
            v += (fl & 0x20) ? d : -d;
        } else if (!(fl & 0x20)) {
            if (p + 2 > len) goto fail;
            v += rd_i16(g + p);
            p += 2;
        }
        ys[i] = v;
    }

    /* the contours -> the pen */
    int start = 0;
    for (int c = 0; c < ncont; ++c) {
        int end = ends[c];
        int count = end - start + 1;
        if (count <= 0) { start = end + 1; continue; }

        /* find the first on-curve point (or make one) */
        int first_on = -1;
        for (int i = 0; i < count; ++i)
            if (flags[start + i] & 1) { first_on = i; break; }

        float sx, sy;
        if (first_on < 0) {
            /* all off-curve: start at the midpoint */
            sx = (xs[start] + xs[start + count - 1]) * 0.5f;
            sy = (ys[start] + ys[start + count - 1]) * 0.5f;
            first_on = 0;
        } else {
            sx = (float)xs[start + first_on];
            sy = (float)ys[start + first_on];
        }
        sdga_pen_move(pen, sx, sy);

        float px = sx, py = sy;         /* the current point */
        float ctrl_x = 0, ctrl_y = 0;
        int have_ctrl = 0;

        for (int k = 1; k <= count; ++k) {
            int i = start + ((first_on + k) % count);
            float gx = (float)xs[i], gy = (float)ys[i];
            int on = (flags[i] & 1) != 0;

            if (on) {
                if (have_ctrl) {
                    sdga_pen_quad(pen, ctrl_x, ctrl_y, gx, gy);
                    have_ctrl = 0;
                } else {
                    sdga_pen_line(pen, gx, gy);
                }
            } else {
                if (have_ctrl) {
                    /* two off-curve points in a row: the implied on-curve */
                    float mx = (ctrl_x + gx) * 0.5f;
                    float my = (ctrl_y + gy) * 0.5f;
                    sdga_pen_quad(pen, ctrl_x, ctrl_y, mx, my);
                }
                ctrl_x = gx; ctrl_y = gy;
                have_ctrl = 1;
            }
            px = gx; py = gy;
        }
        (void)px; (void)py;

        if (have_ctrl)
            sdga_pen_quad(pen, ctrl_x, ctrl_y, sx, sy);
        sdga_pen_close(pen);
        start = end + 1;
    }

    free(xs); free(ys); free(flags); free(ends);
    return 0;

fail:
    free(xs); free(ys); free(flags); free(ends);
    return -1;
}

/* reads a composite glyph into the pen */
static int glyf_composite(sdga_font *f, const unsigned char *g, size_t len,
                          sdga_pen *pen, int depth) {
    size_t p = 10;
    while (p + 4 <= len) {
        unsigned flags = rd_u16(g + p);
        unsigned idx = rd_u16(g + p + 2);
        p += 4;

        float dx = 0, dy = 0;
        if (flags & 0x0001) {          /* ARG_1_AND_2_ARE_WORDS */
            if (p + 4 > len) return -1;
            dx = (float)rd_i16(g + p);
            dy = (float)rd_i16(g + p + 2);
            p += 4;
        } else {
            if (p + 2 > len) return -1;
            dx = (float)(signed char)g[p];
            dy = (float)(signed char)g[p + 1];
            p += 2;
        }

        float a = 1.0f, b = 0.0f, c = 0.0f, d = 1.0f;
        if (flags & 0x0008) {          /* WE_HAVE_A_SCALE */
            if (p + 2 > len) return -1;
            a = d = (float)rd_i16(g + p) / 16384.0f;
            p += 2;
        } else if (flags & 0x0040) {   /* X_AND_Y_SCALE */
            if (p + 4 > len) return -1;
            a = (float)rd_i16(g + p) / 16384.0f;
            d = (float)rd_i16(g + p + 2) / 16384.0f;
            p += 4;
        } else if (flags & 0x0080) {   /* TWO_BY_TWO */
            if (p + 8 > len) return -1;
            a = (float)rd_i16(g + p) / 16384.0f;
            b = (float)rd_i16(g + p + 2) / 16384.0f;
            c = (float)rd_i16(g + p + 4) / 16384.0f;
            d = (float)rd_i16(g + p + 6) / 16384.0f;
            p += 8;
        }

        if (flags & 0x0002) {          /* ARGS_ARE_XY_VALUES */
            /* the transform is applied to the component's coordinates */
            const unsigned char *cg;
            size_t clen;
            if (!glyf_range(f, (int)idx, &cg, &clen)) return -1;
            if (depth < 4) {
                int ncont = rd_i16(cg);
                if (ncont >= 0) {
                    if (glyf_simple(f, cg, clen, pen) != 0) return -1;
                } else if (glyf_composite(f, cg, clen, pen, depth + 1) != 0) {
                    return -1;
                }
            }
        } else {
            /* the point matching is not supported - ignore the component */
            if (!(flags & 0x0004)) {
                if (p + 2 > len) return -1;
                p += 2;
            }
        }
        (void)dx; (void)dy; (void)a; (void)b; (void)c; (void)d;
        if (!(flags & 0x0020)) break;    /* MORE_COMPONENTS */
    }
    return 0;
}

static int font_outline(sdga_font *f, int gid, sdga_pen *pen) {
    const unsigned char *g;
    size_t len;
    if (!glyf_range(f, gid, &g, &len)) return -1;
    if (len < 10) return -1;
    int ncont = rd_i16(g);
    if (ncont >= 0) return glyf_simple(f, g, len, pen);
    return glyf_composite(f, g, len, pen, 0);
}

/* ==================================================================== */
/*                          the atlas builder                            */
/* ==================================================================== */

static int rasterize_glyph(sdga_font *f, int gid, float scale,
                           unsigned char **out_pixels,
                           int *out_w, int *out_h, int *out_bx, int *out_by) {
    sdga_pen pen;
    sdga_pen_init(&pen, scale);

    int rc = f->is_cff ? sdga__cff_outline(f, gid, &pen)
                       : font_outline(f, gid, &pen);
    if (rc != 0 || pen.n < 8) {
        sdga_pen_free(&pen);
        return -1;
    }

    /* the bounding box */
    float minx = 1e30f, miny = 1e30f, maxx = -1e30f, maxy = -1e30f;
    for (int e = 0; e < pen.n / 4; ++e) {
        float x0 = pen.e[e * 4], y0 = pen.e[e * 4 + 1];
        float x1 = pen.e[e * 4 + 2], y1 = pen.e[e * 4 + 3];
        if (x0 < minx) minx = x0;
        if (x1 < minx) minx = x1;
        if (x0 > maxx) maxx = x0;
        if (x1 > maxx) maxx = x1;
        if (y0 < miny) miny = y0;
        if (y1 < miny) miny = y1;
        if (y0 > maxy) maxy = y0;
        if (y1 > maxy) maxy = y1;
    }
    sdga_pen_free(&pen);

    int ox = (int)floorf(minx);
    int oy = (int)floorf(miny);
    int w = (int)ceilf(maxx) - ox;
    int h = (int)ceilf(maxy) - oy;
    if (w <= 0 || h <= 0 || w > 512 || h > 512) {
        sdga_pen_free(&pen);
        return -1;
    }

    /* rasterize into a temporary buffer */
    float *cov = (float *)calloc((size_t)w * (size_t)h, sizeof(float));
    if (!cov) return -1;

    sdga_pen pen2;
    sdga_pen_init(&pen2, scale);
    rc = f->is_cff ? sdga__cff_outline(f, gid, &pen2)
                   : font_outline(f, gid, &pen2);
    if (rc == 0)
        rasterize(&pen2, cov, w, h, (float)ox, (float)oy);
    sdga_pen_free(&pen2);
    if (rc != 0) { free(cov); return -1; }

    unsigned char *pix = (unsigned char *)malloc((size_t)w * (size_t)h);
    if (!pix) { free(cov); return -1; }
    for (int i = 0; i < w * h; ++i) {
        float v = cov[i];
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        pix[i] = (unsigned char)(v * 255.0f + 0.5f);
    }
    free(cov);

    *out_pixels = pix;
    *out_w = w;
    *out_h = h;
    *out_bx = ox;
    *out_by = -oy;          /* the distance from the baseline up */
    return 0;
}

/* The atlas is shared by all the faces: every face (one per used size)
 * packs its glyphs into the same texture, so the UI still draws
 * everything in a single draw call. */
static int atlas_place(sdga_font *f, const unsigned char *bm, int bw, int bh,
                       float *u0, float *v0, float *u1, float *v1,
                       int *rect) {
    if (bw <= 0 || bh <= 0) return 0;
    /* the packer state is shared by all the faces: the faces must not
     * overwrite each other's glyphs in the atlas */
    if (f->shelf_x + bw + 1 > f->tex_w) {
        f->shelf_x = 1;
        f->shelf_y += f->shelf_h + 1;
        f->shelf_h = 0;
    }
    if (f->shelf_y + bh + 1 > f->tex_h) return -1;       /* the atlas is full */
    for (int y = 0; y < bh; ++y)
        memcpy(f->pixels + (size_t)(f->shelf_y + y) * (size_t)f->tex_w
               + (size_t)f->shelf_x, bm + (size_t)y * (size_t)bw, (size_t)bw);
    if (rect) {
        rect[0] = f->shelf_x;
        rect[1] = f->shelf_y;
        rect[2] = bw;
        rect[3] = bh;
    }
    *u0 = (float)f->shelf_x / (float)f->tex_w;
    *u1 = (float)(f->shelf_x + bw) / (float)f->tex_w;
    *v0 = (float)f->shelf_y / (float)f->tex_h;
    *v1 = (float)(f->shelf_y + bh) / (float)f->tex_h;
    f->shelf_x += bw + 1;
    if (bh > f->shelf_h) f->shelf_h = bh;
    return 0;
}

/* recomputes the uv of every glyph (the atlas may have grown) */
static void atlas_refresh_uvs(sdga_font *f) {
    for (int i = 0; i < f->num_faces; ++i) {
        sdga_glyph *glyphs = f->faces[i].glyphs;
        int *rects = f->faces[i].rects;
        if (!glyphs || !rects) continue;
        int n = f->faces[i].num_glyphs;
        for (int g = 0; g < n; ++g) {
            if (glyphs[g].w <= 0 || glyphs[g].h <= 0) continue;
            int x = rects[g * 4 + 0], y = rects[g * 4 + 1];
            int w = rects[g * 4 + 2], h = rects[g * 4 + 3];
            glyphs[g].u0 = (float)x / (float)f->tex_w;
            glyphs[g].u1 = (float)(x + w) / (float)f->tex_w;
            glyphs[g].v0 = (float)y / (float)f->tex_h;
            glyphs[g].v1 = (float)(y + h) / (float)f->tex_h;
        }
    }
}

/* grows the atlas (the pixels are kept) */
static int atlas_grow(sdga_font *f, int w, int h) {
    unsigned char *np = (unsigned char *)calloc((size_t)w * (size_t)h, 1);
    if (!np) return -1;
    if (f->pixels) {
        int cw = f->tex_w < w ? f->tex_w : w;
        int ch = f->tex_h < h ? f->tex_h : h;
        for (int y = 0; y < ch; ++y)
            memcpy(np + (size_t)y * (size_t)w, f->pixels + (size_t)y * (size_t)f->tex_w,
                   (size_t)cw);
        free(f->pixels);
    }
    f->pixels = np;
    f->tex_w = w;
    f->tex_h = h;
    /* the uv of the glyphs changed with the new size */
    atlas_refresh_uvs(f);
    return 0;
}

/* rasterizes the ASCII range at `size` into a new face */
static int build_face(sdga_font *f, float size) {
    if (f->num_faces >= SDGE_FONT_MAX_FACES) return -1;
    if (size <= 0.0f) size = 7.0f;

    float scale = size / (float)f->units_per_em;
    const int first_cp = 32, last_cp = 255;

    sdga_glyph *glyphs = (sdga_glyph *)calloc((size_t)f->num_glyphs,
                                             sizeof(sdga_glyph));
    int *rects = (int *)calloc((size_t)f->num_glyphs * 4, sizeof(int));
    if (!glyphs || !rects) { free(glyphs); free(rects); return -1; }
    int cp_map[256];
    for (int i = 0; i < 256; ++i) cp_map[i] = -1;

    if (f->tex_w == 0 && atlas_grow(f, 512, 512) != 0) {
        free(glyphs); free(rects);
        return -1;
    }

    /* the face is registered before the glyphs are placed: the atlas may
     * grow in the middle of the work and then the uv of the glyphs that
     * are already placed have to be recomputed */
    int face = f->num_faces++;
    f->faces[face].size = size;
    f->faces[face].num_glyphs = f->num_glyphs;
    f->faces[face].glyphs = glyphs;
    f->faces[face].rects = rects;

    int ok = 0;
    for (int cp = first_cp; cp <= last_cp; ++cp) {
        int gid = cmap_lookup(f, (unsigned)cp);
        if (gid <= 0 || gid >= f->num_glyphs) continue;

        unsigned char *bm = NULL;
        int bw = 0, bh = 0, bx = 0, by = 0;
        int has_bitmap = (rasterize_glyph(f, gid, scale, &bm, &bw, &bh,
                                         &bx, &by) == 0);

        /* the advance and the bearings exist even for an empty glyph
         * (the space has no bitmap but it still takes room) */
        sdga_glyph *g = &glyphs[gid];
        int adv = 0;
        if (f->hmtx && f->num_hmetrics > 0) {
            if (gid < f->num_hmetrics) {
                if ((size_t)gid * 4 + 2 <= f->hmtx_len)
                    adv = (int)rd_u16(f->hmtx + (size_t)gid * 4);
            } else if (f->hmtx_len >= 4) {
                adv = (int)rd_u16(f->hmtx + (size_t)(f->num_hmetrics - 1) * 4);
            }
        }
        g->advance = (float)adv * scale;

        if (has_bitmap) {
            float u0, v0, u1, v1;
            if (atlas_place(f, bm, bw, bh, &u0, &v0, &u1, &v1,
                            &rects[gid * 4]) != 0) {
                /* the atlas is full: grow it and try again */
                int nw = f->tex_w * 2, nh = f->tex_h * 2;
                if (atlas_grow(f, nw, nh) != 0) {
                    free(bm);
                    break;
                }
                if (atlas_place(f, bm, bw, bh, &u0, &v0, &u1, &v1,
                                &rects[gid * 4]) != 0) {
                    free(bm);
                    break;                  /* the atlas is full */
                }
            }
            g->w = bw;
            g->h = bh;
            g->bearing_x = bx;
            g->bearing_y = by;
            g->u0 = u0; g->v0 = v0; g->u1 = u1; g->v1 = v1;
        }
        free(bm);
        cp_map[cp] = gid;
        ok++;
    }

    if (!ok) {
        f->num_faces--;
        free(glyphs); free(rects);
        return -1;
    }

    memcpy(f->faces[face].cp_to_glyph, cp_map, sizeof(cp_map));
    return 0;
}

/* ==================================================================== */
/*                              the public API                           */
/* ==================================================================== */

sdga_font *sdga_load_font(const char *path, float pixel_size) {
    if (!path || pixel_size <= 0.0f) return NULL;

    unsigned char *data = NULL;
    size_t size = 0;
    if (sdga_read_file(path, &data, &size) != 0) {
        fprintf(stderr, "font: cannot read '%s'\n", path);
        return NULL;
    }
    if (size < 12) { free(data); return NULL; }

    sdga_font *f = (sdga_font *)calloc(1, sizeof(*f));
    if (!f) { free(data); return NULL; }
    f->data = data;
    f->size = size;
    f->pixel_size = pixel_size;

    /* the container */
    unsigned long ver = rd_u32(data);
    if (ver == 0x74746366u) {                 /* 'ttcf' - a collection */
        if (size < 16) { free(data); free(f); return NULL; }
        unsigned long off = rd_u32(data + 12);
        if (off + 12 > size) { free(data); free(f); return NULL; }
        data += off;
        f->data = data;
        f->size = size - off;
        ver = rd_u32(data);
    }
    if (ver != 0x00010000u && ver != 0x74727565u /*true*/ &&
        ver != 0x4F54544Fu /*OTTO*/) {
        fprintf(stderr, "font: '%s' is not a TTF/OTF file\n", path);
        free(data); free(f);
        return NULL;
    }

    find_table(f, "head", &f->head, &f->head_len);
    find_table(f, "hhea", &f->hhea, &f->hhea_len);
    find_table(f, "hmtx", &f->hmtx, &f->hmtx_len);
    find_table(f, "maxp", &f->maxp, &f->maxp_len);
    find_table(f, "cmap", &f->cmap, &f->cmap_len);
    find_table(f, "loca", &f->loca, &f->loca_len);
    find_table(f, "glyf", &f->glyf, &f->glyf_len);
    find_table(f, "CFF ", &f->cff, &f->cff_len);

    if (!f->head || f->head_len < 54 || !f->hhea || f->hhea_len < 36 ||
        !f->maxp || f->maxp_len < 6 || !f->cmap) {
        fprintf(stderr, "font: '%s' misses a required table\n", path);
        free(data); free(f);
        return NULL;
    }

    f->units_per_em = (int)rd_u16(f->head + 18);
    if (f->units_per_em <= 0) f->units_per_em = 1000;
    f->loc_format = (int)rd_i16(f->head + 50);
    f->num_glyphs = (int)rd_u16(f->maxp + 4);
    f->ascent = rd_i16(f->hhea + 4);
    f->descent = rd_i16(f->hhea + 6);
    f->line_gap = rd_i16(f->hhea + 8);
    /* numberOfHMetrics is the last field of 'hhea' (offset 34) */
    f->num_hmetrics = (int)rd_u16(f->hhea + 34);
    if (f->num_hmetrics <= 0) f->num_hmetrics = f->num_glyphs;
    f->is_cff = (f->cff != NULL);
    f->scale = pixel_size / (float)f->units_per_em;

    snprintf(f->name, sizeof(f->name), "%s", path);

    if (build_face(f, pixel_size) != 0) {
        fprintf(stderr, "font: '%s' has no usable glyphs\n", path);
        free(data); free(f);
        return NULL;
    }
    return f;
}

void sdga_free_font(sdga_font *f) {
    if (!f) return;
    free(f->data);
    free(f->pixels);
    for (int i = 0; i < f->num_faces; ++i) {
        free(f->faces[i].glyphs);
        free(f->faces[i].rects);
    }
    free(f);
}

/* ------------------------------------------------------------- the faces */

int sdga_font_add_face(sdga_font *f, float pixel_size) {
    if (!f || pixel_size <= 0.0f) return -1;
    /* an existing face of the same size is reused */
    for (int i = 0; i < f->num_faces; ++i)
        if (fabsf(f->faces[i].size - pixel_size) < 0.01f) return 0;
    return build_face(f, pixel_size);
}

int sdga_font_face_count(const sdga_font *f) { return f ? f->num_faces : 0; }

float sdga_font_face_size(const sdga_font *f, int face) {
    if (!f || face < 0 || face >= f->num_faces) return 0.0f;
    return f->faces[face].size;
}

int sdga_font_face_for(const sdga_font *f, float size) {
    if (!f || f->num_faces <= 0) return -1;
    int best = 0;
    float best_diff = 1e30f;
    for (int i = 0; i < f->num_faces; ++i) {
        float d = fabsf(f->faces[i].size - size);
        if (d < best_diff) { best_diff = d; best = i; }
    }
    return best;
}

const sdga_glyph *sdga_font_face_glyph(const sdga_font *f, int face,
                                       unsigned codepoint) {
    if (!f || face < 0 || face >= f->num_faces) return NULL;
    if (codepoint > 255) return NULL;
    int gid = f->faces[face].cp_to_glyph[codepoint];
    if (gid < 0) return NULL;
    return &f->faces[face].glyphs[gid];
}

int sdga_font_glyph_index(const sdga_font *f, unsigned codepoint) {
    if (!f) return 0;
    if (f->name[0] && strcmp(f->name, "builtin") == 0) {
        if (codepoint < 32 || codepoint > 126) return 0;
        return (int)codepoint - 32;
    }
    return cmap_lookup(f, codepoint);
}

const sdga_glyph *sdga_font_glyph(const sdga_font *f, int glyph_id) {
    if (!f || f->num_faces <= 0) return NULL;
    if (glyph_id < 0 || glyph_id >= f->faces[0].num_glyphs) return NULL;
    return &f->faces[0].glyphs[glyph_id];
}

int sdga_font_glyph_count(const sdga_font *f) {
    return (f && f->num_faces > 0) ? f->faces[0].num_glyphs : 0;
}

float sdga_font_ascent(const sdga_font *f) {
    if (!f) return 0.0f;
    return (float)f->ascent * f->scale;
}

float sdga_font_descent(const sdga_font *f) {
    if (!f) return 0.0f;
    return (float)(-f->descent) * f->scale;
}

float sdga_font_line_height(const sdga_font *f) {
    if (!f) return 0.0f;
    return (float)(f->ascent - f->descent + f->line_gap) * f->scale;
}

const unsigned char *sdga_font_pixels(const sdga_font *f) {
    return f ? f->pixels : NULL;
}

int sdga_font_texture_size(const sdga_font *f, int *w, int *h) {
    if (!f) return -1;
    if (w) *w = f->tex_w;
    if (h) *h = f->tex_h;
    return 0;
}

const char *sdga_font_name(const sdga_font *f) {
    return f ? f->name : "";
}

/* ==================================================================== */
/*                       the built-in 5x7 bitmap font                    */
/* ==================================================================== */

/* Classic 5x7 font, ASCII 32..126. One glyph = 5 columns, each column a
 * 7 bit mask (bit 0 = top row). Public domain. */
static const unsigned char g_font5x7[95][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /*   */
    {0x00,0x00,0x5F,0x00,0x00}, /* ! */
    {0x00,0x07,0x00,0x07,0x00}, /* " */
    {0x14,0x7F,0x14,0x7F,0x14}, /* # */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* $ */
    {0x23,0x13,0x08,0x64,0x62}, /* % */
    {0x36,0x49,0x55,0x22,0x50}, /* & */
    {0x00,0x05,0x03,0x00,0x00}, /* ' */
    {0x00,0x1C,0x22,0x41,0x00}, /* ( */
    {0x00,0x41,0x22,0x1C,0x00}, /* ) */
    {0x08,0x2A,0x1C,0x2A,0x08}, /* * */
    {0x08,0x08,0x3E,0x08,0x08}, /* + */
    {0x00,0x50,0x30,0x00,0x00}, /* , */
    {0x08,0x08,0x08,0x08,0x08}, /* - */
    {0x00,0x60,0x60,0x00,0x00}, /* . */
    {0x20,0x10,0x08,0x04,0x02}, /* / */
    {0x3E,0x51,0x49,0x45,0x3E}, /* 0 */
    {0x00,0x42,0x7F,0x40,0x00}, /* 1 */
    {0x42,0x61,0x51,0x49,0x46}, /* 2 */
    {0x21,0x41,0x45,0x4B,0x31}, /* 3 */
    {0x18,0x14,0x12,0x7F,0x10}, /* 4 */
    {0x27,0x45,0x45,0x45,0x39}, /* 5 */
    {0x3C,0x4A,0x49,0x49,0x30}, /* 6 */
    {0x01,0x71,0x09,0x05,0x03}, /* 7 */
    {0x36,0x49,0x49,0x49,0x36}, /* 8 */
    {0x06,0x49,0x49,0x29,0x1E}, /* 9 */
    {0x00,0x36,0x36,0x00,0x00}, /* : */
    {0x00,0x56,0x36,0x00,0x00}, /* ; */
    {0x00,0x08,0x14,0x22,0x41}, /* < */
    {0x14,0x14,0x14,0x14,0x14}, /* = */
    {0x41,0x22,0x14,0x08,0x00}, /* > */
    {0x02,0x01,0x51,0x09,0x06}, /* ? */
    {0x32,0x49,0x79,0x41,0x3E}, /* @ */
    {0x7E,0x11,0x11,0x11,0x7E}, /* A */
    {0x7F,0x49,0x49,0x49,0x36}, /* B */
    {0x3E,0x41,0x41,0x41,0x22}, /* C */
    {0x7F,0x41,0x41,0x22,0x1C}, /* D */
    {0x7F,0x49,0x49,0x49,0x41}, /* E */
    {0x7F,0x09,0x09,0x09,0x01}, /* F */
    {0x3E,0x41,0x49,0x49,0x7A}, /* G */
    {0x7F,0x08,0x08,0x08,0x7F}, /* H */
    {0x00,0x41,0x7F,0x41,0x00}, /* I */
    {0x20,0x40,0x41,0x3F,0x01}, /* J */
    {0x7F,0x08,0x14,0x22,0x41}, /* K */
    {0x7F,0x40,0x40,0x40,0x40}, /* L */
    {0x7F,0x02,0x0C,0x02,0x7F}, /* M */
    {0x7F,0x04,0x08,0x10,0x7F}, /* N */
    {0x3E,0x41,0x41,0x41,0x3E}, /* O */
    {0x7F,0x09,0x09,0x09,0x06}, /* P */
    {0x3E,0x41,0x51,0x21,0x5E}, /* Q */
    {0x7F,0x09,0x19,0x29,0x46}, /* R */
    {0x46,0x49,0x49,0x49,0x31}, /* S */
    {0x01,0x01,0x7F,0x01,0x01}, /* T */
    {0x3F,0x40,0x40,0x40,0x3F}, /* U */
    {0x1F,0x20,0x40,0x20,0x1F}, /* V */
    {0x3F,0x40,0x38,0x40,0x3F}, /* W */
    {0x63,0x14,0x08,0x14,0x63}, /* X */
    {0x07,0x08,0x70,0x08,0x07}, /* Y */
    {0x61,0x51,0x49,0x45,0x43}, /* Z */
    {0x00,0x7F,0x41,0x41,0x00}, /* [ */
    {0x02,0x04,0x08,0x10,0x20}, /* \ */
    {0x00,0x41,0x41,0x7F,0x00}, /* ] */
    {0x04,0x02,0x01,0x02,0x04}, /* ^ */
    {0x40,0x40,0x40,0x40,0x40}, /* _ */
    {0x00,0x01,0x02,0x04,0x00}, /* ` */
    {0x20,0x54,0x54,0x54,0x78}, /* a */
    {0x7F,0x48,0x44,0x44,0x38}, /* b */
    {0x38,0x44,0x44,0x44,0x20}, /* c */
    {0x38,0x44,0x44,0x48,0x7F}, /* d */
    {0x38,0x54,0x54,0x54,0x18}, /* e */
    {0x08,0x7E,0x09,0x01,0x02}, /* f */
    {0x0C,0x52,0x52,0x52,0x3E}, /* g */
    {0x7F,0x08,0x04,0x04,0x78}, /* h */
    {0x00,0x44,0x7D,0x40,0x00}, /* i */
    {0x20,0x40,0x44,0x3D,0x00}, /* j */
    {0x7F,0x10,0x28,0x44,0x00}, /* k */
    {0x00,0x41,0x7F,0x40,0x00}, /* l */
    {0x7C,0x04,0x18,0x04,0x78}, /* m */
    {0x7C,0x08,0x04,0x04,0x78}, /* n */
    {0x38,0x44,0x44,0x44,0x38}, /* o */
    {0x7C,0x14,0x14,0x14,0x08}, /* p */
    {0x08,0x14,0x14,0x18,0x7C}, /* q */
    {0x7C,0x08,0x04,0x04,0x08}, /* r */
    {0x48,0x54,0x54,0x54,0x20}, /* s */
    {0x04,0x3F,0x44,0x40,0x20}, /* t */
    {0x3C,0x40,0x40,0x20,0x7C}, /* u */
    {0x1C,0x20,0x40,0x20,0x1C}, /* v */
    {0x3C,0x40,0x30,0x40,0x3C}, /* w */
    {0x44,0x28,0x10,0x28,0x44}, /* x */
    {0x0C,0x50,0x50,0x50,0x3C}, /* y */
    {0x44,0x64,0x54,0x4C,0x44}, /* z */
    {0x00,0x08,0x36,0x41,0x00}, /* { */
    {0x00,0x00,0x7F,0x00,0x00}, /* | */
    {0x00,0x41,0x36,0x08,0x00}, /* } */
    {0x08,0x04,0x08,0x10,0x08}, /* ~ */
};

#define BUILTIN_FIRST 32
#define BUILTIN_LAST  126
#define BUILTIN_COLS  16
#define BUILTIN_ROWS   6
#define BUILTIN_CELL_W 8
#define BUILTIN_CELL_H 8

sdga_font *sdga_load_builtin_font(float pixel_size) {
    (void)pixel_size;                 /* the bitmap font is scaled by the UI */

    sdga_font *f = (sdga_font *)calloc(1, sizeof(*f));
    if (!f) return NULL;

    f->pixel_size = 7.0f;
    f->units_per_em = 1000;
    f->scale = 1.0f;                  /* one font pixel = one screen pixel */
    f->num_glyphs = BUILTIN_LAST - BUILTIN_FIRST + 1;
    f->ascent = 7;                    /* the cap height */
    f->descent = 0;
    f->line_gap = 0;
    snprintf(f->name, sizeof(f->name), "builtin");

    int tex_w = BUILTIN_COLS * BUILTIN_CELL_W;
    int tex_h = BUILTIN_ROWS * BUILTIN_CELL_H;
    unsigned char *pixels = (unsigned char *)calloc((size_t)tex_w * (size_t)tex_h, 1);
    sdga_glyph *glyphs = (sdga_glyph *)calloc((size_t)f->num_glyphs, sizeof(sdga_glyph));
    if (!pixels || !glyphs) {
        free(pixels); free(glyphs); free(f);
        return NULL;
    }

    for (int i = 0; i < f->num_glyphs; ++i) {
        int index = i;
        int col = index % BUILTIN_COLS;
        int row = index / BUILTIN_COLS;
        int ox = col * BUILTIN_CELL_W;
        int oy = row * BUILTIN_CELL_H;
        const unsigned char *g = g_font5x7[index];
        for (int c = 0; c < 5; ++c)
            for (int r = 0; r < 7; ++r)
                if (g[c] & (1u << r))
                    pixels[(size_t)(oy + r) * (size_t)tex_w + (size_t)(ox + c)] = 255;

        sdga_glyph *gl = &glyphs[i];
        gl->w = 5;
        gl->h = 7;
        gl->bearing_x = 0;
        gl->bearing_y = 7;                  /* in pixels */
        gl->advance = 6.0f;
        gl->u0 = (float)ox / (float)tex_w;
        gl->v0 = (float)oy / (float)tex_h;
        gl->u1 = (float)(ox + 5) / (float)tex_w;
        gl->v1 = (float)(oy + 7) / (float)tex_h;
    }

    f->pixels = pixels;
    f->tex_w = tex_w;
    f->tex_h = tex_h;

    int face = f->num_faces++;
    f->faces[face].size = 7.0f;
    f->faces[face].num_glyphs = f->num_glyphs;
    f->faces[face].glyphs = glyphs;
    for (int i = 0; i < f->num_glyphs; ++i)
        f->faces[face].cp_to_glyph[BUILTIN_FIRST + i] = i;
    return f;
}
