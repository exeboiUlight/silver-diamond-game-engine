/* json.h - Silver Diamond Game Engine :: minimal JSON scanner (header-only)
 *
 * Token-based scanner in the style of jsmn: zero allocations, tokens are
 * slices into the caller's buffer. Designed for glTF parsing where the JSON
 * part is small and the heavy data lives in the binary chunk.
 *
 * Token semantics (fixed, self-consistent):
 *   - token.parent = index of the CONTAINING object/array (-1 for root)
 *   - object.size = number of direct tokens inside = 2 * key-value pairs
 *   - array.size  = number of direct elements
 *   - strings include neither quote; keys and plain strings are SDJ_STRING
 *
 * Navigation:
 *   int v = sdj_obj_get(&doc, root, "meshes");     // array token
 *   int e = sdj_arr_at(&doc, v, 0);                // first element
 *   double x = sdj_num(&doc, sdj_obj_get(&doc, e, "count"));
 */
#ifndef SDGE_JSON_H
#define SDGE_JSON_H

#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SDJ_MAX_TOKENS 4096

typedef enum {
    SDJ_UNDEFINED = 0,
    SDJ_OBJECT,
    SDJ_ARRAY,
    SDJ_STRING,     /* quoted string, value excludes the quotes */
    SDJ_PRIMITIVE, /* number, true/false/null */
} sdj_type;

typedef struct {
    sdj_type type;
    int start;      /* offset into the source text */
    int end;        /* exclusive; for containers set at close */
    int size;       /* direct children count (see semantics above) */
    int parent;     /* containing object/array index, -1 for root */
} sdj_token;

typedef struct {
    const char *src;   /* full json text */
    sdj_token  *tok;   /* token array */
    int         count; /* parsed token count, <0 on error */
} sdj_doc;

/* ------------------------------ parser ------------------------------ */
SDGE_INLINE int sdj_parse(const char *js, sdj_token *tok, int max) {
    int pos = 0, tp = 0, dtop = -1;
    while (js[pos] != '\0') {
        char c = js[pos];
        if (c == '{' || c == '[') {
            if (tp >= max) return -1;
            tok[tp].type   = (c == '{') ? SDJ_OBJECT : SDJ_ARRAY;
            tok[tp].start  = pos;
            tok[tp].end   = 0;
            tok[tp].size  = 0;
            tok[tp].parent= dtop;
            if (dtop >= 0) tok[dtop].size++;
            dtop = tp;
            pos++; tp++;
        } else if (c == '}' || c == ']') {
            sdj_type match = (c == '}') ? SDJ_OBJECT : SDJ_ARRAY;
            if (dtop < 0 || tok[dtop].type != match) return -3;
            tok[dtop].end = pos + 1;
            dtop = tok[dtop].parent;
            pos++;
        } else if (c == '"') {
            if (tp >= max) return -1;
            tok[tp].type   = SDJ_STRING;
            tok[tp].start  = pos + 1;
            tok[tp].parent= dtop;
            tok[tp].size  = 0;
            if (dtop >= 0) tok[dtop].size++;
            pos++;
            while (js[pos] != '\0' && js[pos] != '"') {
                if (js[pos] == '\\' && js[pos + 1] != '\0') pos++;
                pos++;
            }
            if (js[pos] != '"') return -4;           /* unterminated string */
            tok[tp].end = pos;
            pos++; tp++;
        } else if (c == ' ' || c == '\t' || c == '\r' || c == '\n'
                   || c == ',' || c == ':') {
            pos++;
        } else {
            if (tp >= max) return -1;
            tok[tp].type   = SDJ_PRIMITIVE;
            tok[tp].start  = pos;
            tok[tp].parent= dtop;
            tok[tp].size  = 0;
            if (dtop >= 0) tok[dtop].size++;
            while (js[pos] != '\0' && js[pos] != ',' && js[pos] != '}'
                   && js[pos] != ']' && js[pos] != ' ' && js[pos] != '\t'
                   && js[pos] != '\r' && js[pos] != '\n')
                pos++;
            if (pos == tok[tp].start) return -5;     /* unexpected character */
            tok[tp].end = pos;
            tp++;
        }
    }
    if (dtop != -1) return -6;                       /* unclosed container */
    return tp;
}

SDGE_INLINE void sdj_init(sdj_doc *d, const char *text, sdj_token *tokbuf) {
    d->src = text;
    d->tok = tokbuf;
    d->count = sdj_parse(text, tokbuf, SDJ_MAX_TOKENS);
}

/* --------------------------- navigation ---------------------------- */

/* how many tokens the subtree rooted at t occupies INCLUDING t itself */
SDGE_INLINE int sdj_subtree_tokens(const sdj_doc *d, int t) {
    int n = 1, i = t + 1;
    while (i < d->count) {
        int p = d->tok[i].parent;
        int inside = 0;
        while (p >= 0) {
            if (p == t) { inside = 1; break; }
            p = d->tok[p].parent;
        }
        if (!inside) break;
        ++n; ++i;
    }
    return n;
}

/* value token of `key` inside object token `obj`; -1 when missing */
SDGE_INLINE int sdj_obj_get(const sdj_doc *d, int obj, const char *key) {
    if (obj < 0 || d->tok[obj].type != SDJ_OBJECT) return -1;
    int klen = (int)strlen(key);
    int i = obj + 1;
    int pairs = d->tok[obj].size / 2;
    for (int p = 0; p < pairs && i < d->count; ++p) {
        if (d->tok[i].type != SDJ_STRING) return -1;
        const sdj_token *k = &d->tok[i];
        if (k->end - k->start == klen
            && memcmp(d->src + k->start, key, (size_t)klen) == 0)
            return i + 1;                              /* value follows key */
        i += 1 + sdj_subtree_tokens(d, i + 1);         /* skip value subtree */
    }
    return -1;
}

/* index of the idx-th element of array token `arr`; -1 when out of range */
SDGE_INLINE int sdj_arr_at(const sdj_doc *d, int arr, int idx) {
    if (arr < 0 || d->tok[arr].type != SDJ_ARRAY) return -1;
    int i = arr + 1;
    int n = d->tok[arr].size;
    for (int e = 0; e < n && i < d->count; ++e) {
        if (e == idx) return i;
        i += sdj_subtree_tokens(d, i);
    }
    return -1;
}

SDGE_INLINE int sdj_len(const sdj_doc *d, int t) {
    if (t < 0) return 0;
    return d->tok[t].size;
}

/* scalar as double (PRIMITIVE only), 0 on failure */
SDGE_INLINE double sdj_num(const sdj_doc *d, int t) {
    if (t < 0 || t >= d->count) return 0.0;
    const sdj_token *k = &d->tok[t];
    if (k->type != SDJ_PRIMITIVE) return 0.0;
    char buf[64];
    int n = k->end - k->start;
    if (n <= 0 || n >= (int)sizeof(buf)) return 0.0;
    memcpy(buf, d->src + k->start, (size_t)n);
    buf[n] = '\0';
    return strtod(buf, NULL);
}

SDGE_INLINE int sdj_is_num(const sdj_doc *d, int t) {
    if (t < 0 || t >= d->count || d->tok[t].type != SDJ_PRIMITIVE) return 0;
    char c = d->src[d->tok[t].start];
    return c == '-' || (c >= '0' && c <= '9');
}

/* string value copied into buf (max bufn incl. NUL), returns length or -1 */
SDGE_INLINE int sdj_str(const sdj_doc *d, int t, char *buf, int bufn) {
    if (t < 0 || t >= d->count || d->tok[t].type != SDJ_STRING) return -1;
    int n = d->tok[t].end - d->tok[t].start;
    if (n > bufn - 1) n = bufn - 1;
    memcpy(buf, d->src + d->tok[t].start, (size_t)n);
    buf[n] = '\0';
    return n;
}

#ifdef __cplusplus
}
#endif
#endif /* SDGE_JSON_H */
