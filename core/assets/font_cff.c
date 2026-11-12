/* font_cff.c - Silver Diamond Game Engine :: OpenType/CFF outlines
 *
 * The 'CFF ' table of an OTF file holds Type 2 charstrings. This file
 * parses the CFF structure (the INDEXes, the Top DICT, the Private DICT,
 * the charset and the FDArray) and runs the Type 2 charstring
 * interpreter, which produces the glyph outline for the pen.
 *
 * The charstring coordinates are transformed into the font units (the
 * FontMatrix of the Top DICT times the unitsPerEm), so the pen always
 * works in the same space as the TrueType outlines.
 */
#include "font_internal.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#define CFF_MAX_STACK 48
#define CFF_MAX_DICT_OPS 48
#define CFF_MAX_SUBR_DEPTH 10

typedef struct {
    const unsigned char *data;   /* the object data (the offsets are 1 based) */
    const unsigned char *hdr;    /* the INDEX header (count, offSize, offsets) */
    size_t len;
} cff_index;

typedef struct {
    const unsigned char *data;
    size_t size;

    cff_index charstrings;
    cff_index gsubrs;
    const unsigned char *charset;
    size_t charset_len;
    const unsigned char *fdselect;
    size_t fdselect_len;
    int is_cid;
    int n_glyphs;

    float m[4];              /* the FontMatrix: a, b, c, d */
    float upem;
} cff_font;

/* ------------------------------------------------------------- helpers */

static unsigned cff_u16(const unsigned char *p) {
    return (unsigned)((p[0] << 8) | p[1]);
}

/* the byte offset of the end of an INDEX that starts at `pos`
 * (the header + the offsets + the object data) */
static size_t cff_index_end(const unsigned char *base, size_t len, size_t pos) {
    if (pos + 2 > len) return len;
    unsigned count = cff_u16(base + pos);
    if (count == 0) return pos + 2;
    if (pos + 3 > len) return len;
    unsigned off_size = base[pos + 2];
    if (off_size < 1 || off_size > 4) return len;
    size_t offs_end = pos + 3 + (size_t)(count + 1) * off_size;
    if (offs_end > len) return len;
    /* the last offset gives the total data size (the offsets are 1 based) */
    unsigned long total = 0;
    const unsigned char *last = base + offs_end - off_size;
    for (unsigned k = 0; k < off_size; ++k) total = (total << 8) | last[k];
    size_t end = offs_end + (size_t)total - 1;
    return end > len ? len : end;
}

/* reads an INDEX; the item data starts at `out->data` */
static int cff_read_index(const unsigned char *base, size_t len, size_t pos,
                          cff_index *out) {
    out->data = NULL;
    out->hdr = NULL;
    out->len = 0;
    if (pos + 2 > len) return -1;
    unsigned count = cff_u16(base + pos);
    if (count == 0) { out->hdr = base + pos; return 0; }
    if (pos + 3 > len) return -1;
    unsigned off_size = base[pos + 2];
    if (off_size < 1 || off_size > 4) return -1;
    size_t offs_end = pos + 3 + (size_t)(count + 1) * off_size;
    if (offs_end > len) return -1;

    /* the last offset gives the total size (1 based) */
    unsigned long total = 0;
    const unsigned char *last = base + offs_end - off_size;
    for (unsigned k = 0; k < off_size; ++k) total = (total << 8) | last[k];

    out->hdr = base + pos;
    out->data = base + offs_end;      /* the object i is at data + off[i] - 1 */
    out->len = (size_t)(total > 0 ? total - 1 : 0);
    return 1;
}

/* the item `i` of an INDEX */
static int cff_index_item(const cff_index *idx, int i,
                          const unsigned char **out, size_t *out_len) {
    if (!idx->data || !idx->hdr) return -1;
    unsigned count = cff_u16(idx->hdr);
    if (count == 0 || i < 0 || i >= (int)count) return -1;
    unsigned off_size = idx->hdr[2];
    if (off_size < 1 || off_size > 4) return -1;
    const unsigned char *offs = idx->hdr + 3;
    unsigned long o1 = 0, o2 = 0;
    for (unsigned k = 0; k < off_size; ++k)
        o1 = (o1 << 8) | offs[(size_t)i * off_size + k];
    for (unsigned k = 0; k < off_size; ++k)
        o2 = (o2 << 8) | offs[(size_t)(i + 1) * off_size + k];
    if (o2 <= o1) return -1;
    *out = idx->data + o1 - 1;          /* the offsets are 1 based */
    *out_len = (size_t)(o2 - o1);
    return 0;
}

/* the DICT reader */
typedef struct {
    const unsigned char *p;
    size_t len;
    size_t pos;
} cff_dict;

static void cff_dict_init(cff_dict *d, const unsigned char *p, size_t len) {
    d->p = p; d->len = len; d->pos = 0;
}

/* reads the operands up to the next operator */
static int cff_dict_next(cff_dict *d, double *ops, int max_ops, int *nops) {
    *nops = 0;
    while (d->pos < d->len) {
        unsigned b0 = d->p[d->pos];
        if (b0 <= 21) {
            d->pos++;
            int op = (int)b0;
            if (b0 == 12) {
                if (d->pos >= d->len) return -1;
                op = 1200 + (int)d->p[d->pos++];
            }
            return op;
        }
        if (b0 == 28) {
            if (d->pos + 3 > d->len) return -1;
            if (*nops < max_ops)
                ops[(*nops)++] = (double)(short)cff_u16(d->p + d->pos + 1);
            d->pos += 3;
            continue;
        }
        if (b0 == 29) {
            if (d->pos + 5 > d->len) return -1;
            long v = ((long)d->p[d->pos + 1] << 24) | ((long)d->p[d->pos + 2] << 16) |
                     ((long)d->p[d->pos + 3] << 8) | (long)d->p[d->pos + 4];
            if (*nops < max_ops) ops[(*nops)++] = (double)v;
            d->pos += 5;
            continue;
        }
        if (b0 == 30) {                        /* a BCD real */
            char buf[64];
            size_t n = 0;
            d->pos++;
            int done = 0;
            while (d->pos < d->len && !done && n < sizeof(buf) - 1) {
                unsigned byte = d->p[d->pos++];
                for (int half = 0; half < 2 && !done; ++half) {
                    unsigned nib = half ? (byte & 0x0F) : (byte >> 4);
                    if (nib <= 9) buf[n++] = (char)('0' + nib);
                    else if (nib == 0x0A) buf[n++] = '.';
                    else if (nib == 0x0B) buf[n++] = 'E';
                    else if (nib == 0x0C) buf[n++] = 'E';
                    else if (nib == 0x0E) buf[n++] = '-';
                    else if (nib == 0x0F) { done = 1; break; }
                    else buf[n++] = '0';
                }
            }
            buf[n] = '\0';
            if (*nops < max_ops) ops[(*nops)++] = atof(buf);
            continue;
        }
        if (b0 >= 32 && b0 <= 246) {
            if (*nops < max_ops) ops[(*nops)++] = (double)((int)b0 - 139);
            d->pos++;
            continue;
        }
        if (b0 >= 247 && b0 <= 250) {
            if (d->pos + 2 > d->len) return -1;
            int v = (int)((b0 - 247) * 256) + (int)d->p[d->pos + 1] + 108;
            if (*nops < max_ops) ops[(*nops)++] = (double)v;
            d->pos += 2;
            continue;
        }
        if (b0 >= 251 && b0 <= 254) {
            if (d->pos + 2 > d->len) return -1;
            int v = -((int)((b0 - 251) * 256) + (int)d->p[d->pos + 1] + 108);
            if (*nops < max_ops) ops[(*nops)++] = (double)v;
            d->pos += 2;
            continue;
        }
        return -1;
    }
    return -1;
}

/* ------------------------------------------------------------ the state */

typedef struct {
    cff_font *f;
    sdga_pen *pen;
    float  stack[CFF_MAX_STACK];
    int    sp;
    float  x, y;              /* the current point (font units) */
    int    n_stems;
    int    width_parsed;
    int    depth;
    int    ended;
    int    hint_mask_bytes;
} cff_state;

static int cff_bias(int count) {
    if (count < 1240) return 107;
    if (count < 33900) return 1131;
    return 32768;
}

/* the pen helpers (they are defined below the interpreter) */
static void cff_pen_move(cff_state *st, float x, float y);
static void cff_pen_line(cff_state *st, float x, float y);
static void cff_pen_cubic(cff_state *st, float c1x, float c1y,
                          float c2x, float c2y, float ex, float ey);

/* transforms a charstring point into the font units */
static void cff_xf(cff_state *st, float x, float y, float *ox, float *oy) {
    cff_font *f = st->f;
    *ox = (f->m[0] * x + f->m[2] * y) * f->upem;
    *oy = (f->m[1] * x + f->m[3] * y) * f->upem;
}

/* ---------------------------------------------------------------- curves */

/* The horizontal curve: dxa dxb dyb dyc [dxc]
 *   c1 = (dxa, 0), c2 = (dxb, dyb), the end = (dxc, dyc)
 * The optional fifth argument is the horizontal delta of the end point.
 * Returns the number of the consumed arguments. */
static int cff_hcurve(cff_state *st, const float *a, int n) {
    float dxa = a[0], dxb = a[1], dyb = a[2], dyc = a[3];
    float dxc = 0.0f;
    int used = 4;
    if (n == 5) { dxc = a[4]; used = 5; }
    float x1 = st->x + dxa, y1 = st->y;
    float x2 = x1 + dxb,    y2 = y1 + dyb;
    float ex = x2 + dxc,    ey = y2 + dyc;
    cff_pen_cubic(st, x1, y1, x2, y2, ex, ey);
    return used;
}

/* The vertical curve: dya dxb dyb dxc [dyc]
 *   c1 = (0, dya), c2 = (dxb, dyb), the end = (dxc, dyc) */
static int cff_vcurve(cff_state *st, const float *a, int n) {
    float dya = a[0], dxb = a[1], dyb = a[2], dxc = a[3];
    float dyc = 0.0f;
    int used = 4;
    if (n == 5) { dyc = a[4]; used = 5; }
    float x1 = st->x,     y1 = st->y + dya;
    float x2 = x1 + dxb,   y2 = y1 + dyb;
    float ex = x2 + dxc,   ey = y2 + dyc;
    cff_pen_cubic(st, x1, y1, x2, y2, ex, ey);
    return used;
}

/* The width may be present in the first stack clearing operator: it is
 * the first argument of the stack. `even_odd` is 1 for the operators
 * that normally take an odd number of the arguments (hmoveto, vmoveto).
 * The function returns the number of the remaining arguments. */
static int cff_pop_width(cff_state *st, int even_odd) {
    if (!st->width_parsed) {
        st->width_parsed = 1;
        if (even_odd ^ (st->sp & 1)) {
            if (st->sp > 0) {
                for (int i = 1; i < st->sp; ++i) st->stack[i - 1] = st->stack[i];
                st->sp--;
            }
        }
    }
    return st->sp;
}

typedef struct {
    cff_index lsubrs; int lbias;
    cff_index gsubrs; int gbias;
} cff_env;

static void cff_run(cff_state *st, const unsigned char *cs, size_t len,
                    const cff_env *env);

static void cff_call_subr(cff_state *st, int which, const cff_env *env) {
    if (st->sp < 1) return;
    int idx = (int)st->stack[--st->sp];
    if (which) idx += env->gbias;
    else       idx += env->lbias;
    if (idx < 0) return;

    const cff_index *sub = which ? &env->gsubrs : &env->lsubrs;
    const unsigned char *item;
    size_t item_len;
    if (cff_index_item(sub, idx, &item, &item_len) != 0) return;
    cff_run(st, item, item_len, env);
}

/* The charstring space is converted to the font units on the way to the
 * pen; the interpreter itself works in the charstring space. */
static void cff_pen_move(cff_state *st, float x, float y) {
    float fx, fy;
    cff_xf(st, x, y, &fx, &fy);
    sdga_pen_move(st->pen, fx, fy);
    st->x = x;
    st->y = y;
}

static void cff_pen_line(cff_state *st, float x, float y) {
    float fx, fy;
    cff_xf(st, x, y, &fx, &fy);
    sdga_pen_line(st->pen, fx, fy);
    st->x = x;
    st->y = y;
}

static void cff_pen_cubic(cff_state *st, float c1x, float c1y,
                          float c2x, float c2y, float ex, float ey) {
    float a1x, a1y, a2x, a2y, bx, by;
    cff_xf(st, c1x, c1y, &a1x, &a1y);
    cff_xf(st, c2x, c2y, &a2x, &a2y);
    cff_xf(st, ex, ey, &bx, &by);
    sdga_pen_cubic(st->pen, a1x, a1y, a2x, a2y, bx, by);
    st->x = ex;
    st->y = ey;
}

static void cff_run(cff_state *st, const unsigned char *cs, size_t len,
                    const cff_env *env) {
    if (++st->depth > CFF_MAX_SUBR_DEPTH) { st->depth--; return; }
    size_t pos = 0;

    while (pos < len && !st->ended) {
        unsigned b0 = cs[pos];

        /* a number: in a Type 2 charstring the operand encodings are
         * 28 (16 bit int), 255 (16.16 fixed) and 32..254 - the values
         * 29..31 are OPERATORS (callgsubr, vhcurveto, hvcurveto)! */
        if (b0 == 28 || b0 == 255 || (b0 >= 32 && b0 <= 254)) {
            float v = 0.0f;
            if (b0 == 28) {
                if (pos + 3 > len) break;
                v = (float)(short)cff_u16(cs + pos + 1);
                pos += 3;
            } else if (b0 == 255) {
                if (pos + 5 > len) break;
                long l = ((long)cs[pos + 1] << 24) | ((long)cs[pos + 2] << 16) |
                         ((long)cs[pos + 3] << 8) | (long)cs[pos + 4];
                v = (float)l / 65536.0f;
                pos += 5;
            } else if (b0 <= 246) {
                v = (float)((int)b0 - 139);
                pos += 1;
            } else if (b0 <= 250) {
                if (pos + 2 > len) break;
                v = (float)(((int)b0 - 247) * 256 + (int)cs[pos + 1] + 108);
                pos += 2;
            } else {
                if (pos + 2 > len) break;
                v = (float)(-((int)((b0 - 251) * 256) + (int)cs[pos + 1] + 108));
                pos += 2;
            }
            if (st->sp < CFF_MAX_STACK) st->stack[st->sp++] = v;
            continue;
        }

        /* an operator */
        int op = (int)b0;
        pos++;
        if (op == 12) {
            if (pos >= len) break;
            op = 1200 + (int)cs[pos++];
        }

        if (op >= 1200) {
            /* the escaped operators */
            switch (op) {
            case 1234: {                        /* hflex */
                if (st->sp < 7) { st->sp = 0; break; }
                float dx1 = st->stack[0], dx2 = st->stack[1];
                float dy2 = st->stack[2], dx3 = st->stack[3];
                float dx4 = st->stack[4], dx5 = st->stack[5];
                float dx6 = st->stack[6];
                float dy5 = -dy2;
                float x1 = st->x + dx1, y1 = st->y;
                float x2 = x1 + dx2,    y2 = y1 + dy2;
                float ex = x2 + dx3,    ey = y2;
                cff_pen_cubic(st, x1, y1, x2, y2, ex, ey);
                x1 = ex + dx4;  y1 = ey;
                x2 = x1 + dx5;  y2 = y1 + dy5;
                ex = x2 + dx6;  ey = y2;
                cff_pen_cubic(st, x1, y1, x2, y2, ex, ey);
                st->sp = 0;
                break;
            }
            case 1235: {                        /* flex */
                if (st->sp < 13) { st->sp = 0; break; }
                for (int k = 0; k < 2; ++k) {
                    const float *a = &st->stack[k * 6];
                    float x1 = st->x + a[0], y1 = st->y + a[1];
                    float x2 = x1 + a[2],    y2 = y1 + a[3];
                    float ex = x2 + a[4],    ey = y2 + a[5];
                    cff_pen_cubic(st, x1, y1, x2, y2, ex, ey);
                }
                st->sp = 0;
                break;
            }
            case 1236: {                        /* hflex1 */
                if (st->sp < 9) { st->sp = 0; break; }
                float dx1 = st->stack[0], dy1 = st->stack[1];
                float dx2 = st->stack[2], dy2 = st->stack[3];
                float dx3 = st->stack[4], dx4 = st->stack[5];
                float dx5 = st->stack[6], dy5 = st->stack[7];
                float dx6 = st->stack[8];
                float dy6 = -(dy1 + dy2 + dy5);
                float x1 = st->x + dx1, y1 = st->y + dy1;
                float x2 = x1 + dx2,    y2 = y1 + dy2;
                float ex = x2 + dx3,    ey = y2;
                cff_pen_cubic(st, x1, y1, x2, y2, ex, ey);
                x1 = ex + dx4;  y1 = ey;
                x2 = x1 + dx5;  y2 = y1 + dy5;
                ex = x2 + dx6;  ey = y2 + dy6;
                cff_pen_cubic(st, x1, y1, x2, y2, ex, ey);
                st->sp = 0;
                break;
            }
            case 1237: {                        /* flex1 */
                if (st->sp < 11) { st->sp = 0; break; }
                float dx1 = st->stack[0], dy1 = st->stack[1];
                float dx2 = st->stack[2], dy2 = st->stack[3];
                float dx3 = st->stack[4], dy3 = st->stack[5];
                float dx4 = st->stack[6], dy4 = st->stack[7];
                float dx5 = st->stack[8], dy5 = st->stack[9];
                float d6 = st->stack[10];
                float dx = dx1 + dx2 + dx3 + dx4 + dx5;
                float dy = dy1 + dy2 + dy3 + dy4 + dy5;
                float dx6, dy6;
                if (fabsf(dx) > fabsf(dy)) { dx6 = d6;  dy6 = -dy; }
                else                         { dx6 = -dx; dy6 = d6; }
                float x1 = st->x + dx1, y1 = st->y + dy1;
                float x2 = x1 + dx2,    y2 = y1 + dy2;
                float ex = x2 + dx3,    ey = y2 + dy3;
                cff_pen_cubic(st, x1, y1, x2, y2, ex, ey);
                x1 = ex + dx4;  y1 = ey;
                x2 = x1 + dx5;  y2 = y1 + dy5;
                ex = x2 + dx6;  ey = y2 + dy6;
                cff_pen_cubic(st, x1, y1, x2, y2, ex, ey);
                st->sp = 0;
                break;
            }
            default:
                st->sp = 0;
                break;
            }
            continue;
        }

        switch (op) {
        case 10: {                               /* callsubr */
            if (st->sp < 1) break;
            /* the stack is NOT cleared: the operands stay for the
             * operators that follow the subroutine */
            cff_call_subr(st, 0, env);
            break;
        }
        case 29: {                               /* callgsubr */
            if (st->sp < 1) break;
            cff_call_subr(st, 1, env);
            break;
        }
        case 1: case 3: case 18: case 23: {   /* the stem hints */
            /* the stems come in pairs; the width may be here too */
            cff_pop_width(st, 0);
            st->n_stems += st->sp / 2;
            st->sp = 0;
            break;
        }
        case 4: {                                /* vmoveto */
            cff_pop_width(st, 1);
            if (st->sp < 1) { st->sp = 0; break; }
            st->y += st->stack[0];
            cff_pen_move(st, st->x, st->y);
            st->sp = 0;
            break;
        }
        case 22: {                               /* hmoveto */
            cff_pop_width(st, 1);
            if (st->sp < 1) { st->sp = 0; break; }
            st->x += st->stack[0];
            cff_pen_move(st, st->x, st->y);
            st->sp = 0;
            break;
        }
        case 21: {                               /* rmoveto */
            cff_pop_width(st, 0);
            if (st->sp < 2) { st->sp = 0; break; }
            st->x += st->stack[0];
            st->y += st->stack[1];
            cff_pen_move(st, st->x, st->y);
            st->sp = 0;
            break;
        }
        case 5: {                                /* rlineto */
            for (int i = 0; i + 1 < st->sp; i += 2) {
                st->x += st->stack[i];
                st->y += st->stack[i + 1];
                cff_pen_line(st, st->x, st->y);
            }
            st->sp = 0;
            break;
        }
        case 6: {                                /* hlineto */
            /* the lines alternate: horizontal, vertical, horizontal... */
            int horiz = 1;
            for (int i = 0; i < st->sp; ++i) {
                if (horiz) st->x += st->stack[i];
                else       st->y += st->stack[i];
                cff_pen_line(st, st->x, st->y);
                horiz = !horiz;
            }
            st->sp = 0;
            break;
        }
        case 7: {                                /* vlineto */
            /* the lines alternate: vertical, horizontal, vertical... */
            int horiz = 0;
            for (int i = 0; i < st->sp; ++i) {
                if (horiz) st->x += st->stack[i];
                else       st->y += st->stack[i];
                cff_pen_line(st, st->x, st->y);
                horiz = !horiz;
            }
            st->sp = 0;
            break;
        }
        case 8: {                                /* rrcurveto */
            for (int i = 0; i + 5 < st->sp; i += 6) {
                float x1 = st->x + st->stack[i];
                float y1 = st->y + st->stack[i + 1];
                float x2 = x1 + st->stack[i + 2];
                float y2 = y1 + st->stack[i + 3];
                float ex = x2 + st->stack[i + 4];
                float ey = y2 + st->stack[i + 5];
                cff_pen_cubic(st, x1, y1, x2, y2, ex, ey);
            }
            st->sp = 0;
            break;
        }
        case 24: {                               /* rcurveline */
            int i = 0;
            while (i + 8 <= st->sp) {
                float x1 = st->x + st->stack[i];
                float y1 = st->y + st->stack[i + 1];
                float x2 = x1 + st->stack[i + 2];
                float y2 = y1 + st->stack[i + 3];
                float ex = x2 + st->stack[i + 4];
                float ey = y2 + st->stack[i + 5];
                cff_pen_cubic(st, x1, y1, x2, y2, ex, ey);
                i += 6;
            }
            if (i + 1 < st->sp) {
                st->x += st->stack[i];
                st->y += st->stack[i + 1];
                cff_pen_line(st, st->x, st->y);
            }
            st->sp = 0;
            break;
        }
        case 25: {                               /* rlinecurve */
            int i = 0;
            while (i + 8 <= st->sp) {
                st->x += st->stack[i];
                st->y += st->stack[i + 1];
                cff_pen_line(st, st->x, st->y);
                i += 2;
            }
            if (i + 5 < st->sp) {
                float x1 = st->x + st->stack[i];
                float y1 = st->y + st->stack[i + 1];
                float x2 = x1 + st->stack[i + 2];
                float y2 = y1 + st->stack[i + 3];
                float ex = x2 + st->stack[i + 4];
                float ey = y2 + st->stack[i + 5];
                cff_pen_cubic(st, x1, y1, x2, y2, ex, ey);
            }
            st->sp = 0;
            break;
        }
        case 26: {                               /* vvcurveto */
            /* dx1? {dya dxb dyb dyc}+ */
            int i = 0;
            float dx1 = 0.0f;
            if (st->sp & 1) { dx1 = st->stack[0]; i = 1; }
            for (; i + 3 < st->sp; i += 4) {
                float dya = st->stack[i],     dxb = st->stack[i + 1];
                float dyb = st->stack[i + 2], dyc = st->stack[i + 3];
                float x1 = st->x + dx1, y1 = st->y + dya;
                float x2 = x1 + dxb,    y2 = y1 + dyb;
                float ex = x2,          ey = y2 + dyc;
                cff_pen_cubic(st, x1, y1, x2, y2, ex, ey);
                dx1 = 0.0f;
            }
            st->sp = 0;
            break;
        }
        case 27: {                               /* hhcurveto */
            /* dy1? {dxa dxb dyb dxc}+ */
            int i = 0;
            float dy1 = 0.0f;
            if (st->sp & 1) { dy1 = st->stack[0]; i = 1; }
            for (; i + 3 < st->sp; i += 4) {
                float dxa = st->stack[i],     dxb = st->stack[i + 1];
                float dyb = st->stack[i + 2], dxc = st->stack[i + 3];
                float x1 = st->x + dxa, y1 = st->y + dy1;
                float x2 = x1 + dxb,    y2 = y1 + dyb;
                float ex = x2 + dxc,    ey = y2;
                cff_pen_cubic(st, x1, y1, x2, y2, ex, ey);
                dy1 = 0.0f;
            }
            st->sp = 0;
            break;
        }
        case 30: {                               /* vhcurveto */
            /* the curves alternate: vertical, horizontal, vertical...
             * The trailing delta (the last argument) belongs to the
             * curve when only one argument is left after it */
            int i = 0, vert = 1;
            while (i < st->sp) {
                int n = st->sp - i;
                if (n < 4) break;
                i += vert ? cff_vcurve(st, &st->stack[i], n)
                          : cff_hcurve(st, &st->stack[i], n);
                vert = !vert;
            }
            st->sp = 0;
            break;
        }
        case 31: {                               /* hvcurveto */
            /* the curves alternate: horizontal, vertical, horizontal... */
            int i = 0, vert = 0;
            while (i < st->sp) {
                int n = st->sp - i;
                if (n < 4) break;
                i += vert ? cff_vcurve(st, &st->stack[i], n)
                          : cff_hcurve(st, &st->stack[i], n);
                vert = !vert;
            }
            st->sp = 0;
            break;
        }
        case 14:                                 /* endchar */
            /* the contour is closed by the endchar */
            sdga_pen_close(st->pen);
            st->ended = 1;
            cff_pop_width(st, 0);
            st->sp = 0;
            break;
        case 19: case 20: {                      /* hintmask cntrmask */
            /* hintmask and cntrmask may be preceded by an implicit vstem
             * declaration: the operands that are still on the stack are
             * the vstem pairs (and possibly the width).  The mask length
             * depends on the total number of the stems, so it has to be
             * recomputed whenever new stems arrive. */
            if (st->sp > 0) {
                cff_pop_width(st, 0);
                st->n_stems += st->sp / 2;
                st->hint_mask_bytes = (st->n_stems + 7) / 8;
            }
            if (!st->hint_mask_bytes) st->hint_mask_bytes = 1;
            st->sp = 0;
            pos += (size_t)st->hint_mask_bytes;
            break;
        }
        case 11:                                 /* return */
            /* the end of the subroutine: leave the current cff_run */
            st->depth--;
            return;
        default:
            st->sp = 0;
            break;
        }
    }
    st->depth--;
}

/* --------------------------------------------------------- the charset */

/* the CID of the glyph (a CID keyed font) */
static int cff_charset_cid(cff_font *f, int gid) {
    if (!f->charset) return gid;
    const unsigned char *p = f->charset;
    size_t len = f->charset_len;
    if (len < 3) return gid;
    unsigned fmt = p[2];
    if (fmt == 0) {
        size_t off = 3 + (size_t)gid * 2;
        if (off + 2 > len) return 0;
        return (int)cff_u16(p + off);
    }
    if (fmt == 1 || fmt == 2) {
        size_t pos = 3;
        int gid_cur = 1;
        int nleft = f->n_glyphs - 1;
        while (nleft > 0 && pos + 3 <= len) {
            int n = (int)cff_u16(p + pos);
            if (n <= 0) break;
            int sid = (int)cff_u16(p + pos + 2);
            if (gid_cur <= gid && gid < gid_cur + n) return sid + (gid - gid_cur);
            gid_cur += n;
            nleft -= n;
            pos += (fmt == 1) ? 3 : 4;
        }
    }
    return gid;
}

/* ---------------------------------------------------------------- entry */

int sdga__cff_outline(struct sdga_font *f, int gid, sdga_pen *pen) {
    if (!f->cff || f->cff_len < 4) return -1;


    const unsigned char *base = f->cff;
    size_t len = f->cff_len;

    cff_font cf;
    memset(&cf, 0, sizeof(cf));
    cf.upem = (float)f->units_per_em;
    cf.n_glyphs = f->num_glyphs;
    cf.m[0] = 0.001f;
    cf.m[3] = 0.001f;

    /* the header: major, minor, hdrSize, offSize */
    size_t pos = 4;

    /* the Name, Top DICT, String and Global Subr INDEXes */
    cff_index names, topdicts, strings, gsubrs;
    size_t pos_name = pos;
    int r_names = cff_read_index(base, len, pos, &names);
    pos = cff_index_end(base, len, pos);
    size_t pos_top = pos;
    int r_top = cff_read_index(base, len, pos, &topdicts);
    pos = cff_index_end(base, len, pos);
    size_t pos_str = pos;
    int r_str = cff_read_index(base, len, pos, &strings);
    pos = cff_index_end(base, len, pos);
    size_t pos_gsub = pos;
    int r_gsub = cff_read_index(base, len, pos, &gsubrs);
    pos = cff_index_end(base, len, pos);
    if (r_names < 0 || r_top < 0 || r_str < 0 || r_gsub < 0) return -1;

    /* the Top DICT */
    const unsigned char *dict;
    size_t dict_len;
    if (cff_index_item(&topdicts, 0, &dict, &dict_len) != 0) return -1;

    unsigned long charstrings_off = 0, private_size = 0, private_off = 0;
    unsigned long charset_off = 0, fdselect_off = 0;
    double fm[6] = { 0.001, 0, 0, 0.001, 0, 0 };
    int have_fm = 0;

    {
        cff_dict d;
        cff_dict_init(&d, dict, dict_len);
        double ops[CFF_MAX_DICT_OPS];
        int nops, op;
        while ((op = cff_dict_next(&d, ops, CFF_MAX_DICT_OPS, &nops)) >= 0) {
            switch (op) {
            case 17: if (nops > 0) charstrings_off = (unsigned long)ops[0]; break;
            case 18:
                if (nops >= 2) {
                    private_size = (unsigned long)ops[0];
                    private_off  = (unsigned long)ops[1];
                }
                break;
            case 15: if (nops > 0) charset_off = (unsigned long)ops[0]; break;
            case 1236: if (nops > 0) fdselect_off = (unsigned long)ops[0]; break;
            case 1207:
                if (nops >= 6) {
                    for (int i = 0; i < 6; ++i) fm[i] = ops[i];
                    have_fm = 1;
                }
                break;
            case 1230: cf.is_cid = 1; break;        /* ROS */
            default: break;
            }
        }
    }
    if (!have_fm) { fm[0] = 0.001; fm[3] = 0.001; }
    cf.m[0] = (float)fm[0];
    cf.m[1] = (float)fm[1];
    cf.m[2] = (float)fm[2];
    cf.m[3] = (float)fm[3];

    /* the CharStrings */
    if (!charstrings_off || charstrings_off >= len) return -1;
    if (cff_read_index(base, len, (size_t)charstrings_off, &cf.charstrings) <= 0)
        return -1;
    cf.gsubrs = gsubrs;

    if (charset_off && charset_off < len) {
        cf.charset = base + charset_off;
        cf.charset_len = len - (size_t)charset_off;
    }
    if (fdselect_off && fdselect_off < len) {
        cf.fdselect = base + fdselect_off;
        cf.fdselect_len = len - (size_t)fdselect_off;
    }

    /* the Private DICT and the local subrs */
    cff_index lsubrs;
    memset(&lsubrs, 0, sizeof(lsubrs));
    int lcount = 0;
    if (private_off && private_off + private_size <= len) {
        cff_dict pd;
        cff_dict_init(&pd, base + private_off, (size_t)private_size);
        double pops[CFF_MAX_DICT_OPS];
        int pnops, pop;
        unsigned long subrs_off = 0;
        while ((pop = cff_dict_next(&pd, pops, CFF_MAX_DICT_OPS, &pnops)) >= 0)
            if (pop == 19 && pnops > 0) subrs_off = (unsigned long)pops[0];
        if (subrs_off) {
            size_t so = (size_t)private_off + (size_t)subrs_off;
            if (so + 2 <= len) {
                lcount = (int)cff_u16(base + so);
                cff_index ls;
                if (cff_read_index(base, len, so, &ls) > 0) lsubrs = ls;
            }
        }
    }

    /* the charstring of the glyph */
    int cs_index = gid;
    if (cf.is_cid) cs_index = cff_charset_cid(&cf, gid);

    const unsigned char *cs;
    size_t cs_len;
    if (cff_index_item(&cf.charstrings, cs_index, &cs, &cs_len) != 0) return -1;


    cff_env env;
    env.lsubrs = lsubrs;
    env.lbias = cff_bias(lcount);
    env.gsubrs = gsubrs;
    /* the global subr count (the INDEX header is at its own position) */
    env.gbias = cff_bias((int)cff_u16(base + pos_gsub));
    (void)pos_name; (void)pos_top; (void)pos_str;

    cff_state st;
    memset(&st, 0, sizeof(st));
    st.f = &cf;
    st.pen = pen;
    cff_run(&st, cs, cs_len, &env);

    return pen->n > 0 ? 0 : -1;
}
