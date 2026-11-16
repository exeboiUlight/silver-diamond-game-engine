/* shader_lang.c - Silver Diamond Game Engine :: simple shader language
 *
 * A tiny GLSL-like language compiled into a GLSL 330 vertex/fragment pair.
 * The language is intentionally small: uniforms, an optional vertex block,
 * a fragment block, typed local declarations, assignments, if/else,
 * discard, arithmetic and the usual vector/math built-ins.
 */
#include "shader_lang.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------------- tokens */
typedef enum {
    TK_EOF = 0,
    TK_IDENT,
    TK_NUMBER,
    TK_LPAREN, TK_RPAREN, TK_LBRACE, TK_RBRACE,
    TK_SEMI, TK_COMMA, TK_DOT,
    TK_PLUS, TK_MINUS, TK_STAR, TK_SLASH, TK_PERCENT,
    TK_ASSIGN, TK_PLUS_ASSIGN, TK_MINUS_ASSIGN,
    TK_STAR_ASSIGN, TK_SLASH_ASSIGN,
    TK_LT, TK_LE, TK_GT, TK_GE, TK_EQ, TK_NE,
    TK_AND, TK_OR, TK_NOT
} tok_kind;

typedef struct {
    tok_kind kind;
    char     text[SDSL_NAME_MAX];
    int      line, col;
} token;

typedef struct {
    token *items;
    int    count, cap;
} tokvec;

static int tokvec_push(tokvec *v, const token *t) {
    if (v->count >= v->cap) {
        int cap = v->cap ? v->cap * 2 : 128;
        token *p = (token *)realloc(v->items, sizeof(token) * (size_t)cap);
        if (!p) return 0;
        v->items = p;
        v->cap = cap;
    }
    v->items[v->count++] = *t;
    return 1;
}

/* --------------------------------------------------------- string buffer */
typedef struct {
    char  *data;
    size_t len, cap;
    int    failed;
} sbuf;

static void sb_reserve(sbuf *b, size_t extra) {
    if (b->failed) return;
    size_t need = b->len + extra + 1;
    if (need <= b->cap) return;
    size_t cap = b->cap ? b->cap * 2 : 1024;
    while (cap < need) cap *= 2;
    char *p = (char *)realloc(b->data, cap);
    if (!p) { b->failed = 1; return; }
    b->data = p;
    b->cap = cap;
}

static void sb_putc(sbuf *b, char c) {
    sb_reserve(b, 1);
    if (b->failed) return;
    b->data[b->len++] = c;
    b->data[b->len] = '\0';
}

static void sb_puts(sbuf *b, const char *s) {
    size_t n = strlen(s);
    sb_reserve(b, n);
    if (b->failed) return;
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

static void sb_printf(sbuf *b, const char *fmt, ...) {
    char tmp[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    sb_puts(b, tmp);
}

static void sb_free(sbuf *b) {
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
    b->failed = 0;
}

/* ----------------------------------------------------------------- lexer */
typedef struct {
    const char *src;
    int         len, pos;
    int         line, col;
    tokvec      toks;
    char        error[256];
    int         failed;
} lexer;

static void lex_error(lexer *l, int line, int col, const char *fmt, ...) {
    if (l->failed) return;
    char msg[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    snprintf(l->error, sizeof(l->error), "%d:%d: %s", line, col, msg);
    l->failed = 1;
}

static char lex_peek(lexer *l, int ahead) {
    int i = l->pos + ahead;
    return (i >= 0 && i < l->len) ? l->src[i] : '\0';
}

static char lex_take(lexer *l) {
    char c = l->src[l->pos++];
    if (c == '\n') { l->line++; l->col = 1; }
    else           { l->col++; }
    return c;
}

static void lex_skip_space(lexer *l) {
    for (;;) {
        char c = lex_peek(l, 0);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            lex_take(l);
        } else if (c == '/' && lex_peek(l, 1) == '/') {
            while (lex_peek(l, 0) && lex_peek(l, 0) != '\n') lex_take(l);
        } else if (c == '/' && lex_peek(l, 1) == '*') {
            int line = l->line, col = l->col;
            lex_take(l); lex_take(l);
            while (lex_peek(l, 0)) {
                if (lex_peek(l, 0) == '*' && lex_peek(l, 1) == '/') {
                    lex_take(l); lex_take(l);
                    break;
                }
                lex_take(l);
            }
            if (!lex_peek(l, 0) && !(l->len >= 2 &&
                l->src[l->len - 2] == '*' && l->src[l->len - 1] == '/')) {
                lex_error(l, line, col, "unterminated block comment");
            }
        } else {
            return;
        }
    }
}

static int lex_ident(lexer *l, token *t) {
    int n = 0;
    while (n < SDSL_NAME_MAX - 1) {
        char c = lex_peek(l, 0);
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_')) break;
        t->text[n++] = lex_take(l);
    }
    if (n == SDSL_NAME_MAX - 1 && lex_peek(l, 0)) {
        while (lex_peek(l, 0)) {
            char c = lex_peek(l, 0);
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_')) break;
            lex_take(l);
        }
        lex_error(l, t->line, t->col, "identifier is too long");
        return 0;
    }
    t->text[n] = '\0';
    return 1;
}

static int lex_number(lexer *l, token *t) {
    int n = 0;
    if (lex_peek(l, 0) == '.') t->text[n++] = lex_take(l);
    while (lex_peek(l, 0) >= '0' && lex_peek(l, 0) <= '9')
        if (n < SDSL_NAME_MAX - 1) t->text[n++] = lex_take(l);
        else { lex_take(l); n++; }
    if (lex_peek(l, 0) == '.') {
        if (n < SDSL_NAME_MAX - 1) t->text[n++] = lex_take(l);
        else { lex_take(l); n++; }
        while (lex_peek(l, 0) >= '0' && lex_peek(l, 0) <= '9')
            if (n < SDSL_NAME_MAX - 1) t->text[n++] = lex_take(l);
            else { lex_take(l); n++; }
    }
    if (lex_peek(l, 0) == 'e' || lex_peek(l, 0) == 'E') {
        if (n < SDSL_NAME_MAX - 1) t->text[n++] = lex_take(l);
        else { lex_take(l); n++; }
        if (lex_peek(l, 0) == '+' || lex_peek(l, 0) == '-') {
            if (n < SDSL_NAME_MAX - 1) t->text[n++] = lex_take(l);
            else { lex_take(l); n++; }
        }
        while (lex_peek(l, 0) >= '0' && lex_peek(l, 0) <= '9')
            if (n < SDSL_NAME_MAX - 1) t->text[n++] = lex_take(l);
            else { lex_take(l); n++; }
    }
    if (n >= SDSL_NAME_MAX - 1) {
        lex_error(l, t->line, t->col, "number literal is too long");
        return 0;
    }
    t->text[n] = '\0';
    return 1;
}

static int lex_operator(lexer *l, token *t) {
    char c = lex_take(l);
    char d = lex_peek(l, 0);
    t->kind = TK_EOF;
    switch (c) {
    case '(': t->kind = TK_LPAREN; break;
    case ')': t->kind = TK_RPAREN; break;
    case '{': t->kind = TK_LBRACE; break;
    case '}': t->kind = TK_RBRACE; break;
    case ';': t->kind = TK_SEMI; break;
    case ',': t->kind = TK_COMMA; break;
    case '.': t->kind = TK_DOT; break;
    case '+':
        if (d == '=') { lex_take(l); t->kind = TK_PLUS_ASSIGN; }
        else t->kind = TK_PLUS;
        break;
    case '-':
        if (d == '=') { lex_take(l); t->kind = TK_MINUS_ASSIGN; }
        else t->kind = TK_MINUS;
        break;
    case '*':
        if (d == '=') { lex_take(l); t->kind = TK_STAR_ASSIGN; }
        else t->kind = TK_STAR;
        break;
    case '/':
        if (d == '=') { lex_take(l); t->kind = TK_SLASH_ASSIGN; }
        else t->kind = TK_SLASH;
        break;
    case '%': t->kind = TK_PERCENT; break;
    case '=':
        if (d == '=') { lex_take(l); t->kind = TK_EQ; }
        else t->kind = TK_ASSIGN;
        break;
    case '<':
        if (d == '=') { lex_take(l); t->kind = TK_LE; }
        else t->kind = TK_LT;
        break;
    case '>':
        if (d == '=') { lex_take(l); t->kind = TK_GE; }
        else t->kind = TK_GT;
        break;
    case '!':
        if (d == '=') { lex_take(l); t->kind = TK_NE; }
        else t->kind = TK_NOT;
        break;
    case '&':
        if (d == '&') { lex_take(l); t->kind = TK_AND; }
        else lex_error(l, t->line, t->col, "'&' is not allowed (use '&&')");
        break;
    case '|':
        if (d == '|') { lex_take(l); t->kind = TK_OR; }
        else lex_error(l, t->line, t->col, "'|' is not allowed (use '||')");
        break;
    case '"':
    case '\'':
        lex_error(l, t->line, t->col, "strings are not supported");
        break;
    case '#':
        lex_error(l, t->line, t->col,
                  "preprocessor directives are not supported");
        break;
    default:
        lex_error(l, t->line, t->col, "unexpected character '%c'", c);
        break;
    }
    return !l->failed;
}

static int lex_all(const char *src, lexer *l) {
    memset(l, 0, sizeof(*l));
    l->src = src;
    l->len = (int)strlen(src);
    l->line = 1;
    l->col = 1;

    for (;;) {
        lex_skip_space(l);
        if (l->failed) return 0;
        token t;
        memset(&t, 0, sizeof(t));
        t.line = l->line;
        t.col = l->col;
        char c = lex_peek(l, 0);
        if (!c) {
            t.kind = TK_EOF;
            if (!tokvec_push(&l->toks, &t)) {
                lex_error(l, t.line, t.col, "out of memory");
                return 0;
            }
            return 1;
        }
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
            t.kind = TK_IDENT;
            if (!lex_ident(l, &t)) return 0;
        } else if ((c >= '0' && c <= '9') ||
                   (c == '.' && lex_peek(l, 1) >= '0' &&
                    lex_peek(l, 1) <= '9')) {
            t.kind = TK_NUMBER;
            if (!lex_number(l, &t)) return 0;
        } else {
            if (!lex_operator(l, &t)) return 0;
        }
        if (!tokvec_push(&l->toks, &t)) {
            lex_error(l, t.line, t.col, "out of memory");
            return 0;
        }
    }
}

/* ---------------------------------------------------------------- parser */
#define STAGE_VERTEX   1
#define STAGE_FRAGMENT 2

#define MAX_SYMBOLS 256

typedef struct {
    token *toks;
    int    ntok, pos;

    sbuf   vs_body;
    sbuf   fs_body;
    sbuf  *out;

    int    stage;

    sdsl_uniform uniforms[SDSL_MAX_UNIFORMS];
    int          uniform_count;

    char   vs_syms[MAX_SYMBOLS][SDSL_NAME_MAX];
    int    vs_sym_count;
    char   fs_syms[MAX_SYMBOLS][SDSL_NAME_MAX];
    int    fs_sym_count;

    char   error[512];
    int    failed;
} parser;

static void perr(parser *p, const token *t, const char *fmt, ...) {
    if (p->failed) return;
    char msg[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    snprintf(p->error, sizeof(p->error), "%d:%d: %s",
             t ? t->line : 0, t ? t->col : 0, msg);
    p->failed = 1;
}

static token *p_cur(parser *p) { return &p->toks[p->pos]; }
static token *p_next(parser *p) {
    if (p->toks[p->pos].kind != TK_EOF) p->pos++;
    return &p->toks[p->pos];
}
static token *p_ahead(parser *p, int n) {
    int i = p->pos + n;
    if (i >= p->ntok) i = p->ntok - 1;
    return &p->toks[i];
}
static int p_is(parser *p, tok_kind k) { return p_cur(p)->kind == k; }
static int p_word(const token *t, const char *w) {
    return t->kind == TK_IDENT && strcmp(t->text, w) == 0;
}
static int p_accept(parser *p, tok_kind k) {
    if (!p_is(p, k)) return 0;
    p_next(p);
    return 1;
}
static int p_expect(parser *p, tok_kind k, const char *what) {
    if (p_is(p, k)) { p_next(p); return 1; }
    perr(p, p_cur(p), "expected %s", what);
    return 0;
}

/* ------------------------------------------------------------- symbols */
static int sym_known(const parser *p, int stage, const char *name) {
    const char (*syms)[SDSL_NAME_MAX];
    int count;
    if (stage == STAGE_VERTEX) {
        syms = p->vs_syms; count = p->vs_sym_count;
    } else {
        syms = p->fs_syms; count = p->fs_sym_count;
    }
    for (int i = 0; i < count; ++i)
        if (strcmp(syms[i], name) == 0) return 1;
    return 0;
}

static int sym_add(parser *p, int stage, const char *name) {
    char (*syms)[SDSL_NAME_MAX];
    int *count;
    if (stage == STAGE_VERTEX) {
        syms = p->vs_syms; count = &p->vs_sym_count;
    } else {
        syms = p->fs_syms; count = &p->fs_sym_count;
    }
    if (sym_known(p, stage, name)) return 1;
    if (*count >= MAX_SYMBOLS) {
        perr(p, p_cur(p), "too many variables");
        return 0;
    }
    snprintf(syms[*count], SDSL_NAME_MAX, "%s", name);
    (*count)++;
    return 1;
}

static void sym_to(parser *p, int stage, int count) {
    if (stage == STAGE_VERTEX) p->vs_sym_count = count;
    else                       p->fs_sym_count = count;
}

static int sym_mark(const parser *p, int stage) {
    return stage == STAGE_VERTEX ? p->vs_sym_count : p->fs_sym_count;
}

/* ------------------------------------------------------------- language */
static const char *g_reserved[] = {
    "uv", "uv1", "normal", "vertexNormal", "worldPos", "viewDir",
    "lightDir", "sunDir", "sunColor", "sunIntensity", "ambient",
    "baseColor", "albedo", "metallic", "roughness", "emissive", "alpha",
    "time", "resolution", "cameraPos", "screenUV", "fragCoord",
    "frontFacing", "color", "tangent", "bitangent", "position",
    "occlusion", "pbrLighting", "pbr", "discard", "uniform", "vertex",
    "fragment", "shader", "if", "else", "true", "false", "return",
    "sd_ambient"
};

static int is_reserved(const char *name) {
    for (size_t i = 0; i < sizeof(g_reserved) / sizeof(g_reserved[0]); ++i)
        if (strcmp(g_reserved[i], name) == 0) return 1;
    return 0;
}

static int is_type_word(const char *s) {
    return !strcmp(s, "float") || !strcmp(s, "int") || !strcmp(s, "bool") ||
           !strcmp(s, "vec2")  || !strcmp(s, "vec3") || !strcmp(s, "vec4") ||
           !strcmp(s, "mat2")  || !strcmp(s, "mat3") || !strcmp(s, "mat4") ||
           !strcmp(s, "sampler2D");
}

static int uniform_type_of(const char *s) {
    if (!strcmp(s, "float"))     return SDSL_UNIFORM_FLOAT;
    if (!strcmp(s, "int"))       return SDSL_UNIFORM_INT;
    if (!strcmp(s, "bool"))      return SDSL_UNIFORM_BOOL;
    if (!strcmp(s, "vec2"))      return SDSL_UNIFORM_VEC2;
    if (!strcmp(s, "vec3"))      return SDSL_UNIFORM_VEC3;
    if (!strcmp(s, "vec4"))      return SDSL_UNIFORM_VEC4;
    if (!strcmp(s, "sampler2D")) return SDSL_UNIFORM_SAMPLER2D;
    return 0;
}

static const char *uniform_type_name(int type) {
    switch (type) {
    case SDSL_UNIFORM_FLOAT:     return "float";
    case SDSL_UNIFORM_INT:       return "int";
    case SDSL_UNIFORM_BOOL:      return "bool";
    case SDSL_UNIFORM_VEC2:      return "vec2";
    case SDSL_UNIFORM_VEC3:      return "vec3";
    case SDSL_UNIFORM_VEC4:      return "vec4";
    case SDSL_UNIFORM_SAMPLER2D: return "sampler2D";
    default:                     return "float";
    }
}

static int is_builtin_function(const char *s) {
    static const char *fns[] = {
        "radians", "degrees", "sin", "cos", "tan", "asin", "acos", "atan",
        "sinh", "cosh", "tanh", "pow", "exp", "log", "exp2", "log2",
        "sqrt", "inversesqrt", "abs", "sign", "floor", "trunc", "round",
        "roundEven", "ceil", "fract", "mod", "min", "max", "clamp", "mix",
        "step", "smoothstep", "length", "distance", "dot", "cross",
        "normalize", "faceforward", "reflect", "refract", "texture",
        "textureLod", "textureSize", "texelFetch", "dFdx", "dFdy", "fwidth",
        "vec2", "vec3", "vec4", "mat2", "mat3", "mat4", "float", "int",
        "bool", "pbrLighting", "pbr", "sd_ggx", "sd_fresnel", "sd_ambient"
    };
    for (size_t i = 0; i < sizeof(fns) / sizeof(fns[0]); ++i)
        if (strcmp(fns[i], s) == 0) return 1;
    return 0;
}

static void add_builtins(parser *p, int stage) {
    static const char *fs_names[] = {
        "uv", "uv1", "normal", "vertexNormal", "worldPos", "cameraPos",
        "viewDir", "lightDir", "sunDir", "sunColor", "sunIntensity",
        "ambient", "baseColor", "albedo", "metallic", "roughness",
        "emissive", "alpha", "time", "resolution", "screenUV", "fragCoord",
        "frontFacing", "color", "tangent", "bitangent", "occlusion"
    };
    static const char *vs_names[] = {
        "position", "normal", "uv", "uv1", "tangent", "time", "resolution"
    };
    if (stage == STAGE_FRAGMENT) {
        for (size_t i = 0; i < sizeof(fs_names) / sizeof(fs_names[0]); ++i)
            sym_add(p, stage, fs_names[i]);
    } else {
        for (size_t i = 0; i < sizeof(vs_names) / sizeof(vs_names[0]); ++i)
            sym_add(p, stage, vs_names[i]);
    }
}

/* --------------------------------------------------------- expressions */
static int parse_expr(parser *p);

static int parse_primary(parser *p) {
    token *t = p_cur(p);
    if (p_is(p, TK_NUMBER)) {
        sb_puts(p->out, t->text);
        p_next(p);
        return 1;
    }
    if (p_is(p, TK_IDENT)) {
        if (p_word(t, "true") || p_word(t, "false")) {
            sb_puts(p->out, t->text);
            p_next(p);
            return 1;
        }
        /* function call / constructor */
        if (p_ahead(p, 1)->kind == TK_LPAREN) {
            if (!is_builtin_function(t->text)) {
                perr(p, t, "unknown function '%s'", t->text);
                return 0;
            }
            sb_puts(p->out, t->text);
            p_next(p);
            p_next(p);                     /* '(' */
            sb_putc(p->out, '(');
            if (!p_is(p, TK_RPAREN)) {
                for (;;) {
                    if (!parse_expr(p)) return 0;
                    if (!p_accept(p, TK_COMMA)) break;
                    sb_puts(p->out, ", ");
                }
            }
            if (!p_expect(p, TK_RPAREN, "')'")) return 0;
            sb_putc(p->out, ')');
            return 1;
        }
        /* variable */
        if (t->text[0] != 'g' || t->text[1] != 'l' || t->text[2] != '_') {
            if (!sym_known(p, p->stage, t->text)) {
                perr(p, t, "unknown variable '%s'", t->text);
                return 0;
            }
        }
        sb_puts(p->out, t->text);
        p_next(p);
        return 1;
    }
    if (p_accept(p, TK_LPAREN)) {
        sb_putc(p->out, '(');
        if (!parse_expr(p)) return 0;
        if (!p_expect(p, TK_RPAREN, "')'")) return 0;
        sb_putc(p->out, ')');
        return 1;
    }
    perr(p, t, "expected an expression");
    return 0;
}

static int parse_postfix(parser *p) {
    if (!parse_primary(p)) return 0;
    while (p_accept(p, TK_DOT)) {
        token *m = p_cur(p);
        if (!p_is(p, TK_IDENT)) {
            perr(p, m, "expected a field name after '.'");
            return 0;
        }
        sb_putc(p->out, '.');
        sb_puts(p->out, m->text);
        p_next(p);
    }
    return 1;
}

static int parse_unary(parser *p) {
    if (p_accept(p, TK_MINUS)) {
        sb_puts(p->out, "(-");
        if (!parse_unary(p)) return 0;
        sb_putc(p->out, ')');
        return 1;
    }
    if (p_accept(p, TK_NOT)) {
        sb_puts(p->out, "(!");
        if (!parse_unary(p)) return 0;
        sb_putc(p->out, ')');
        return 1;
    }
    return parse_postfix(p);
}

#define BINARY_LEVEL(fn, nextfn, kind1, op1, kind2, op2, kind3, op3)       \
static int fn(parser *p) {                                                 \
    sb_putc(p->out, '(');                                                   \
    if (!nextfn(p)) return 0;                                               \
    for (;;) {                                                              \
        const char *op = NULL;                                              \
        if (p_is(p, kind1)) op = op1;                                       \
        else if (p_is(p, kind2)) op = op2;                                  \
        else if (p_is(p, kind3)) op = op3;                                  \
        if (!op) break;                                                     \
        p_next(p);                                                          \
        sb_printf(p->out, " %s ", op);                                      \
        if (!nextfn(p)) return 0;                                           \
    }                                                                       \
    sb_putc(p->out, ')');                                                   \
    return 1;                                                               \
}

BINARY_LEVEL(parse_mul, parse_unary,
             TK_STAR, "*", TK_SLASH, "/", TK_PERCENT, "%")
BINARY_LEVEL(parse_add, parse_mul,
             TK_PLUS, "+", TK_MINUS, "-", TK_EOF, NULL)

static int parse_cmp(parser *p) {
    sb_putc(p->out, '(');
    if (!parse_add(p)) return 0;
    for (;;) {
        const char *op = NULL;
        if (p_is(p, TK_LT))      op = "<";
        else if (p_is(p, TK_LE)) op = "<=";
        else if (p_is(p, TK_GT)) op = ">";
        else if (p_is(p, TK_GE)) op = ">=";
        if (!op) break;
        p_next(p);
        sb_printf(p->out, " %s ", op);
        if (!parse_add(p)) return 0;
    }
    sb_putc(p->out, ')');
    return 1;
}

BINARY_LEVEL(parse_eq, parse_cmp,
             TK_EQ, "==", TK_NE, "!=", TK_EOF, NULL)
BINARY_LEVEL(parse_and, parse_eq,
             TK_AND, "&&", TK_EOF, NULL, TK_EOF, NULL)
BINARY_LEVEL(parse_or, parse_and,
             TK_OR, "||", TK_EOF, NULL, TK_EOF, NULL)

static int parse_expr(parser *p) {
    return parse_or(p);
}

/* ------------------------------------------------------------ statements */
static int parse_statement(parser *p);
static int parse_scope(parser *p, int stage, int emit_braces);

static int is_assignment(tok_kind k) {
    return k == TK_ASSIGN || k == TK_PLUS_ASSIGN || k == TK_MINUS_ASSIGN ||
           k == TK_STAR_ASSIGN || k == TK_SLASH_ASSIGN;
}

static const char *assignment_op(tok_kind k) {
    switch (k) {
    case TK_ASSIGN:       return " = ";
    case TK_PLUS_ASSIGN:  return " += ";
    case TK_MINUS_ASSIGN: return " -= ";
    case TK_STAR_ASSIGN:  return " *= ";
    case TK_SLASH_ASSIGN: return " /= ";
    default:              return " = ";
    }
}

static int looks_like_assignment(parser *p) {
    int save = p->pos;
    int found = 0;
    if (p_is(p, TK_IDENT)) {
        p_next(p);
        while (p_accept(p, TK_DOT)) {
            if (!p_is(p, TK_IDENT)) break;
            p_next(p);
        }
        if (is_assignment(p_cur(p)->kind)) found = 1;
    }
    p->pos = save;
    return found;
}

static int parse_assignment(parser *p) {
    token *t = p_cur(p);
    if (!sym_known(p, p->stage, t->text)) {
        perr(p, t, "unknown variable '%s'", t->text);
        return 0;
    }
    sb_puts(p->out, t->text);
    p_next(p);
    while (p_accept(p, TK_DOT)) {
        token *m = p_cur(p);
        if (!p_is(p, TK_IDENT)) {
            perr(p, m, "expected a field name after '.'");
            return 0;
        }
        sb_putc(p->out, '.');
        sb_puts(p->out, m->text);
        p_next(p);
    }
    if (!is_assignment(p_cur(p)->kind)) {
        perr(p, p_cur(p), "expected an assignment operator");
        return 0;
    }
    sb_puts(p->out, assignment_op(p_cur(p)->kind));
    p_next(p);
    if (!parse_expr(p)) return 0;
    if (!p_expect(p, TK_SEMI, "';'")) return 0;
    sb_puts(p->out, ";\n");
    return 1;
}

static int parse_declaration(parser *p) {
    token *type = p_cur(p);
    if (!is_type_word(type->text)) {
        perr(p, type, "expected a type");
        return 0;
    }
    p_next(p);
    token *name = p_cur(p);
    if (!p_is(p, TK_IDENT)) {
        perr(p, name, "expected a variable name");
        return 0;
    }
    if (is_reserved(name->text)) {
        perr(p, name, "'%s' is reserved", name->text);
        return 0;
    }
    if (sym_known(p, p->stage, name->text)) {
        perr(p, name, "'%s' is already declared", name->text);
        return 0;
    }
    p_next(p);
    sb_printf(p->out, "%s %s", type->text, name->text);
    if (p_accept(p, TK_ASSIGN)) {
        sb_puts(p->out, " = ");
        if (!parse_expr(p)) return 0;
    }
    if (!p_expect(p, TK_SEMI, "';'")) return 0;
    sb_puts(p->out, ";\n");
    if (!sym_add(p, p->stage, name->text)) return 0;
    return 1;
}

static int parse_body(parser *p) {
    if (p_is(p, TK_LBRACE)) {
        p_next(p);
        return parse_scope(p, p->stage, 1);
    }
    int stage = p->stage;
    int mark = sym_mark(p, stage);
    sb_puts(p->out, "{\n");
    if (!parse_statement(p)) return 0;
    sb_puts(p->out, "}\n");
    sym_to(p, stage, mark);
    return 1;
}

static int parse_if(parser *p) {
    token *kw = p_cur(p);
    (void)kw;
    p_next(p);
    if (!p_expect(p, TK_LPAREN, "'(' after 'if'")) return 0;
    sb_puts(p->out, "if ");
    if (!parse_expr(p)) return 0;
    if (!p_expect(p, TK_RPAREN, "')'")) return 0;
    sb_putc(p->out, ' ');
    if (!parse_body(p)) return 0;
    if (p_word(p_cur(p), "else")) {
        p_next(p);
        sb_puts(p->out, "else ");
        if (!parse_body(p)) return 0;
    }
    sb_putc(p->out, '\n');
    return 1;
}

static int parse_statement(parser *p) {
    if (p_is(p, TK_LBRACE)) {
        p_next(p);
        return parse_scope(p, p->stage, 1);
    }
    if (p_is(p, TK_IDENT)) {
        token *t = p_cur(p);
        if (p_word(t, "if")) return parse_if(p);
        if (p_word(t, "discard")) {
            if (p->stage != STAGE_FRAGMENT) {
                perr(p, t, "'discard' is only allowed in the fragment block");
                return 0;
            }
            p_next(p);
            if (!p_expect(p, TK_SEMI, "';'")) return 0;
            sb_puts(p->out, "discard;\n");
            return 1;
        }
        if (p_word(t, "return")) {
            if (p->stage != STAGE_FRAGMENT) {
                perr(p, t, "'return' is only allowed in the fragment block");
                return 0;
            }
            p_next(p);
            if (!p_expect(p, TK_SEMI, "';' after 'return'")) return 0;
            /* the fragment output must still be written before leaving */
            sb_puts(p->out, "frag = color;\nreturn;\n");
            return 1;
        }
        if (p_word(t, "uniform")) {
            perr(p, t, "uniforms must be declared at the top level");
            return 0;
        }
        if (p_word(t, "vertex") || p_word(t, "fragment") ||
            p_word(t, "shader")) {
            perr(p, t, "'%s' blocks cannot be nested", t->text);
            return 0;
        }
        if (is_type_word(t->text)) return parse_declaration(p);
    }
    if (looks_like_assignment(p)) return parse_assignment(p);

    if (!parse_expr(p)) return 0;
    if (!p_expect(p, TK_SEMI, "';'")) return 0;
    sb_puts(p->out, ";\n");
    return 1;
}

static int parse_scope(parser *p, int stage, int emit_braces) {
    int old_stage = p->stage;
    sbuf *old_out = p->out;
    p->stage = stage;
    p->out = (stage == STAGE_VERTEX) ? &p->vs_body : &p->fs_body;
    int mark = sym_mark(p, stage);

    if (emit_braces) sb_puts(p->out, "{\n");
    while (!p_is(p, TK_RBRACE) && !p_is(p, TK_EOF)) {
        if (!parse_statement(p)) {
            p->stage = old_stage;
            p->out = old_out;
            return 0;
        }
    }
    if (!p_is(p, TK_RBRACE)) {
        perr(p, p_cur(p), "expected '}'");
        p->stage = old_stage;
        p->out = old_out;
        return 0;
    }
    p_next(p);
    if (emit_braces) sb_puts(p->out, "}\n");
    sym_to(p, stage, mark);
    p->stage = old_stage;
    p->out = old_out;
    return 1;
}

/* -------------------------------------------------------- top level */
static int parse_uniform(parser *p) {
    p_next(p);                             /* 'uniform' */
    token *type_tok = p_cur(p);
    if (!p_is(p, TK_IDENT)) {
        perr(p, type_tok, "expected a uniform type");
        return 0;
    }
    int type = uniform_type_of(type_tok->text);
    if (!type) {
        perr(p, type_tok, "unsupported uniform type '%s'", type_tok->text);
        return 0;
    }
    p_next(p);
    token *name = p_cur(p);
    if (!p_is(p, TK_IDENT)) {
        perr(p, name, "expected a uniform name");
        return 0;
    }
    if (is_reserved(name->text) || is_builtin_function(name->text)) {
        perr(p, name, "'%s' is reserved", name->text);
        return 0;
    }
    for (int i = 0; i < p->uniform_count; ++i) {
        if (strcmp(p->uniforms[i].name, name->text) == 0) {
            perr(p, name, "uniform '%s' is already declared", name->text);
            return 0;
        }
    }
    if (p->uniform_count >= SDSL_MAX_UNIFORMS) {
        perr(p, name, "too many uniforms (max %d)", SDSL_MAX_UNIFORMS);
        return 0;
    }
    snprintf(p->uniforms[p->uniform_count].name, SDSL_NAME_MAX, "%s",
             name->text);
    p->uniforms[p->uniform_count].type = type;
    p->uniform_count++;
    p_next(p);
    if (!p_expect(p, TK_SEMI, "';' after the uniform declaration")) return 0;
    if (!sym_add(p, STAGE_VERTEX, name->text)) return 0;
    if (!sym_add(p, STAGE_FRAGMENT, name->text)) return 0;
    return 1;
}

/* ---------------------------------------------------------- GLSL text */
static const char *F_HEAD =
"#version 330 core\n"
"in vec3 vN;\n"
"in vec3 vWpos;\n"
"in vec2 vUV;\n"
"in vec2 vUV1;\n"
"in vec4 vTan;\n"
"uniform sampler2D uBaseTex;\n"
"uniform sampler2D uMRTex;\n"
"uniform sampler2D uNormalTex;\n"
"uniform sampler2D uOcclusionTex;\n"
"uniform sampler2D uEmissiveTex;\n"
"uniform int uHasBase, uHasMR, uHasNormal, uHasOcclusion, uHasEmissive;\n"
"uniform int uHasTangent;\n"
"uniform mat3 uUVXform[5];\n"
"uniform int uUVSet[5];\n"
"uniform vec3 uCamPos;\n"
"uniform vec3 uAmbient;\n"
"uniform vec3 uSunDir, uSunColor;\n"
"uniform float uSunI;\n"
"uniform vec4 uPointPos[8];\n"
"uniform vec4 uPointCol[8];\n"
"uniform int uPointCount;\n"
"uniform vec4 uBaseColor;\n"
"uniform float uMetallic, uRoughness;\n"
"uniform vec3 uEmissive;\n"
"uniform float uNormalScale, uOcclusionStrength, uAlphaCutoff;\n"
"uniform int uDoubleSided, uAlphaMode;\n"
"uniform float uTime;\n"
"uniform vec2 uResolution;\n";

static const char *F_MAIN =
"out vec4 frag;\n"
"vec3 sd_ambient = vec3(0.0);\n"
"float sd_ggx(float ndh, float a) {\n"
"    float a2 = a * a;\n"
"    float d = ndh * ndh * (a2 - 1.0) + 1.0;\n"
"    return a2 / (3.14159265 * d * d);\n"
"}\n"
"vec2 sd_slotUV(int slot) {\n"
"    vec2 uv = (uUVSet[slot] == 1) ? vUV1 : vUV;\n"
"    return (uUVXform[slot] * vec3(uv, 1.0)).xy;\n"
"}\n"
"mat3 sd_cotangentFrame(vec3 N, vec3 p, vec2 uv) {\n"
"    vec3 dp1 = dFdx(p), dp2 = dFdy(p);\n"
"    vec2 duv1 = dFdx(uv), duv2 = dFdy(uv);\n"
"    vec3 dp2perp = cross(dp2, N);\n"
"    vec3 dp1perp = cross(N, dp1);\n"
"    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;\n"
"    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;\n"
"    float invmax = inversesqrt(max(dot(T, T), dot(B, B)));\n"
"    return mat3(T * invmax, B * invmax, N);\n"
"}\n"
"vec3 sd_fresnel(float c, vec3 f0) {\n"
"    return f0 + (vec3(1.0) - f0) * pow(1.0 - c, 5.0);\n"
"}\n"
"vec3 sd_pbrLighting(vec3 N, vec3 albedo, float metallic, float roughness) {\n"
"    vec3 V = normalize(uCamPos - vWpos);\n"
"    vec3 diffuse = albedo * (1.0 - metallic);\n"
"    vec3 f0 = mix(vec3(0.04), albedo, metallic);\n"
"    float a = roughness * roughness + 1e-3;\n"
"    vec3 col = sd_ambient * albedo;\n"
"    if (uSunI > 0.0) {\n"
"        vec3 L = normalize(-uSunDir);\n"
"        float ndl = max(dot(N, L), 0.0);\n"
"        vec3 H = normalize(L + V);\n"
"        float spec = sd_ggx(max(dot(N, H), 0.0), a);\n"
"        col += uSunColor * uSunI * ndl * (diffuse + f0 * spec);\n"
"    }\n"
"    for (int i = 0; i < uPointCount; ++i) {\n"
"        vec3 d = uPointPos[i].xyz - vWpos;\n"
"        float dist = length(d) + 1e-4;\n"
"        if (dist > uPointPos[i].w) continue;\n"
"        vec3 L = d / dist;\n"
"        float ndl = max(dot(N, L), 0.0);\n"
"        float att = 1.0 - dist / uPointPos[i].w;\n"
"        att *= att;\n"
"        vec3 H = normalize(L + V);\n"
"        float spec = sd_ggx(max(dot(N, H), 0.0), a);\n"
"        col += uPointCol[i].rgb * uPointCol[i].w * att * ndl\n"
"             * (diffuse + f0 * spec);\n"
"    }\n"
"    return col;\n"
"}\n"
"vec3 pbrLighting(vec3 N, vec3 albedo, float metallic, float roughness) {\n"
"    return sd_pbrLighting(N, albedo, metallic, roughness);\n"
"}\n"
"vec3 pbr(vec3 N, vec3 albedo, float metallic, float roughness) {\n"
"    return sd_pbrLighting(N, albedo, metallic, roughness);\n"
"}\n"
"void main() {\n"
"    vec4 base = uBaseColor;\n"
"    if (uHasBase == 1) base *= texture(uBaseTex, sd_slotUV(0));\n"
"    if (uAlphaMode == 1) {\n"
"        if (base.a < uAlphaCutoff) discard;\n"
"    } else if (uAlphaMode == 2) {\n"
"        if (base.a <= 0.0) discard;\n"
"    }\n"
"    float metallic = uMetallic;\n"
"    float roughness = uRoughness;\n"
"    if (uHasMR == 1) {\n"
"        vec4 mr = texture(uMRTex, sd_slotUV(1));\n"
"        roughness *= mr.g;\n"
"        metallic *= mr.b;\n"
"    }\n"
"    float occlusion = 1.0;\n"
"    if (uHasOcclusion == 1)\n"
"        occlusion = mix(1.0, texture(uOcclusionTex, sd_slotUV(3)).r,\n"
"                        uOcclusionStrength);\n"
"    /* matches the built-in PBR pass: ambient is scaled by occlusion */\n"
"    sd_ambient = uAmbient * occlusion;\n"
"    if (roughness < 0.045) roughness = 0.045;\n"
"    vec3 vertexNormal = normalize(vN);\n"
"    vec3 normal = vertexNormal;\n"
"    if (uDoubleSided == 1 && !gl_FrontFacing) normal = -normal;\n"
"    if (uHasNormal == 1) {\n"
"        vec2 nuv = sd_slotUV(2);\n"
"        vec3 nm = texture(uNormalTex, nuv).rgb * 2.0 - 1.0;\n"
"        nm.xy *= uNormalScale;\n"
"        mat3 tbn;\n"
"        if (uHasTangent == 1) {\n"
"            vec3 T = normalize(vTan.xyz - normal * dot(normal, vTan.xyz));\n"
"            vec3 B = cross(normal, T) * (vTan.w < 0.0 ? -1.0 : 1.0);\n"
"            tbn = mat3(T, B, normal);\n"
"        } else {\n"
"            tbn = sd_cotangentFrame(normal, vWpos, nuv);\n"
"        }\n"
"        normal = normalize(tbn * nm);\n"
"    }\n"
"    vec3 tangent = length(vTan.xyz) > 1e-6\n"
"        ? normalize(vTan.xyz) : vec3(1.0, 0.0, 0.0);\n"
"    vec3 bitangent = cross(normal, tangent)\n"
"        * (vTan.w < 0.0 ? -1.0 : 1.0);\n"
"    vec2 uv = vUV;\n"
"    vec2 uv1 = vUV1;\n"
"    vec3 worldPos = vWpos;\n"
"    vec3 cameraPos = uCamPos;\n"
"    vec3 viewDir = normalize(uCamPos - vWpos);\n"
"    vec3 sunDir = uSunDir;\n"
"    vec3 lightDir = normalize(-uSunDir);\n"
"    vec3 sunColor = uSunColor;\n"
"    float sunIntensity = uSunI;\n"
"    vec3 ambient = uAmbient;\n"
"    vec4 baseColor = base;\n"
"    vec3 albedo = base.rgb;\n"
"    vec3 emissive = uEmissive;\n"
"    if (uHasEmissive == 1)\n"
"        emissive *= texture(uEmissiveTex, sd_slotUV(4)).rgb;\n"
"    float alpha = base.a;\n"
"    float time = uTime;\n"
"    vec2 resolution = uResolution;\n"
"    vec2 screenUV = gl_FragCoord.xy / max(uResolution, vec2(1.0));\n"
"    vec3 fragCoord = gl_FragCoord.xyz;\n"
"    bool frontFacing = gl_FrontFacing;\n"
"    vec4 color = vec4(0.0, 0.0, 0.0, alpha);\n";

static const char *F_FOOT =
"    frag = color;\n"
"}\n";

static const char *F_DEFAULT_BODY =
"    color.rgb = pbrLighting(normal, albedo, metallic, roughness) + emissive;\n";

static const char *V_HEAD =
"#version 330 core\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec3 aNrm;\n"
"layout(location=2) in vec4 aJoint;\n"
"layout(location=3) in vec4 aWeight;\n"
"layout(location=4) in vec2 aUV;\n"
"layout(location=5) in vec2 aUV1;\n"
"layout(location=6) in vec4 aTan;\n"
"uniform mat4 uProj, uView, uModel;\n"
"uniform mat4 uJoints[128];\n"
"uniform int uSkinned;\n"
"uniform float uTime;\n"
"uniform vec2 uResolution;\n";

static const char *V_MAIN =
"out vec3 vN;\n"
"out vec3 vWpos;\n"
"out vec2 vUV;\n"
"out vec2 vUV1;\n"
"out vec4 vTan;\n"
"void main() {\n"
"    vec3 position = aPos;\n"
"    vec3 normal = aNrm;\n"
"    vec2 uv = aUV;\n"
"    vec2 uv1 = aUV1;\n"
"    vec4 tangent = aTan;\n"
"    float time = uTime;\n"
"    vec2 resolution = uResolution;\n";

static const char *V_FOOT =
"    mat4 skin = mat4(1.0);\n"
"    if (uSkinned == 1) {\n"
"        skin = uJoints[int(aJoint.x)] * aWeight.x\n"
"             + uJoints[int(aJoint.y)] * aWeight.y\n"
"             + uJoints[int(aJoint.z)] * aWeight.z\n"
"             + uJoints[int(aJoint.w)] * aWeight.w;\n"
"    }\n"
"    mat3 nm3 = mat3(uModel) * mat3(skin);\n"
"    vec4 world = uModel * skin * vec4(position, 1.0);\n"
"    vWpos = world.xyz;\n"
"    vN = nm3 * normal;\n"
"    vUV = uv;\n"
"    vUV1 = uv1;\n"
"    vec3 t = nm3 * tangent.xyz;\n"
"    float tl = length(t);\n"
"    vTan = vec4(tl > 1e-6 ? t / tl : vec3(1.0, 0.0, 0.0), tangent.w);\n"
"    gl_Position = uProj * uView * world;\n"
"}\n";

static int build_program(parser *p, sdsl_program *out,
                         int has_vertex, int has_fragment) {
    sbuf vs, fs;
    memset(&vs, 0, sizeof(vs));
    memset(&fs, 0, sizeof(fs));

    sb_puts(&vs, V_HEAD);
    sb_puts(&fs, F_HEAD);
    for (int i = 0; i < p->uniform_count; ++i) {
        const char *tn = uniform_type_name(p->uniforms[i].type);
        sb_printf(&vs, "uniform %s %s;\n", tn, p->uniforms[i].name);
        sb_printf(&fs, "uniform %s %s;\n", tn, p->uniforms[i].name);
    }
    sb_puts(&vs, V_MAIN);
    sb_puts(&fs, F_MAIN);

    if (has_vertex && p->vs_body.data && p->vs_body.len > 0) {
        sb_puts(&vs, p->vs_body.data);
        if (p->vs_body.len > 0 && p->vs_body.data[p->vs_body.len - 1] != '\n')
            sb_putc(&vs, '\n');
    }
    if (has_fragment && p->fs_body.data && p->fs_body.len > 0) {
        sb_puts(&fs, p->fs_body.data);
        if (p->fs_body.data[p->fs_body.len - 1] != '\n')
            sb_putc(&fs, '\n');
    } else {
        sb_puts(&fs, F_DEFAULT_BODY);
    }
    sb_puts(&vs, V_FOOT);
    sb_puts(&fs, F_FOOT);

    if (vs.failed || fs.failed) {
        sb_free(&vs);
        sb_free(&fs);
        perr(p, NULL, "out of memory while building GLSL");
        return 0;
    }

    memset(out, 0, sizeof(*out));
    out->vertex = vs.data;
    out->fragment = fs.data;
    out->uniform_count = p->uniform_count;
    for (int i = 0; i < p->uniform_count; ++i)
        out->uniforms[i] = p->uniforms[i];
    return 1;
}

static int parse_program(lexer *l, sdsl_program *out, char *error,
                         int error_size) {
    parser p;
    memset(&p, 0, sizeof(p));
    p.toks = l->toks.items;
    p.ntok = l->toks.count;
    p.stage = STAGE_FRAGMENT;
    p.out = &p.fs_body;

    add_builtins(&p, STAGE_VERTEX);
    add_builtins(&p, STAGE_FRAGMENT);

    int wrapped = 0, closed = 0;
    int explicit_v = 0, explicit_f = 0, implicit_f = 0;

    if (p_word(p_cur(&p), "shader")) {
        p_next(&p);
        if (!p_is(&p, TK_IDENT)) {
            perr(&p, p_cur(&p), "expected a shader name after 'shader'");
            goto fail;
        }
        p_next(&p);
        if (!p_expect(&p, TK_LBRACE, "'{' after the shader name")) goto fail;
        wrapped = 1;
    }

    while (!p_is(&p, TK_EOF)) {
        if (wrapped && p_is(&p, TK_RBRACE)) {
            p_next(&p);
            closed = 1;
            break;
        }
        if (p_is(&p, TK_RBRACE)) {
            perr(&p, p_cur(&p), "unexpected '}'");
            goto fail;
        }
        if (p_word(p_cur(&p), "uniform")) {
            if (!parse_uniform(&p)) goto fail;
            continue;
        }
        if (p_word(p_cur(&p), "vertex")) {
            if (explicit_v) {
                perr(&p, p_cur(&p), "duplicate vertex block");
                goto fail;
            }
            if (implicit_f) {
                perr(&p, p_cur(&p),
                     "the vertex block must come before the fragment code");
                goto fail;
            }
            p_next(&p);
            if (!p_expect(&p, TK_LBRACE, "'{' after 'vertex'")) goto fail;
            explicit_v = 1;
            if (!parse_scope(&p, STAGE_VERTEX, 0)) goto fail;
            continue;
        }
        if (p_word(p_cur(&p), "fragment")) {
            if (explicit_f || implicit_f) {
                perr(&p, p_cur(&p), "duplicate fragment block");
                goto fail;
            }
            p_next(&p);
            if (!p_expect(&p, TK_LBRACE, "'{' after 'fragment'")) goto fail;
            explicit_f = 1;
            if (!parse_scope(&p, STAGE_FRAGMENT, 0)) goto fail;
            continue;
        }
        if (explicit_f) {
            perr(&p, p_cur(&p), "unexpected code after the fragment block");
            goto fail;
        }
        implicit_f = 1;
        if (!parse_statement(&p)) goto fail;
    }

    if (wrapped && !closed) {
        perr(&p, p_cur(&p), "expected '}' to close the shader");
        goto fail;
    }
    if (!p_is(&p, TK_EOF)) {
        perr(&p, p_cur(&p), "unexpected token after the shader");
        goto fail;
    }
    if (p.failed) goto fail;

    if (!build_program(&p, out, explicit_v, explicit_f || implicit_f))
        goto fail;

    sb_free(&p.vs_body);
    sb_free(&p.fs_body);
    free(l->toks.items);
    return 0;

fail:
    if (error && error_size > 0) {
        if (p.error[0]) snprintf(error, (size_t)error_size, "%s", p.error);
        else snprintf(error, (size_t)error_size, "shader compilation failed");
    }
    sb_free(&p.vs_body);
    sb_free(&p.fs_body);
    free(l->toks.items);
    memset(out, 0, sizeof(*out));
    return -1;
}

int sdsl_compile(const char *source, sdsl_program *out,
                 char *error, int error_size) {
    if (error && error_size > 0) error[0] = '\0';
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    if (!source) {
        if (error && error_size > 0)
            snprintf(error, (size_t)error_size, "empty shader source");
        return -1;
    }

    lexer l;
    if (!lex_all(source, &l)) {
        if (error && error_size > 0)
            snprintf(error, (size_t)error_size, "%s", l.error);
        free(l.toks.items);
        return -1;
    }
    return parse_program(&l, out, error, error_size);
}

void sdsl_program_free(sdsl_program *p) {
    if (!p) return;
    free(p->vertex);
    free(p->fragment);
    p->vertex = NULL;
    p->fragment = NULL;
    p->uniform_count = 0;
}



