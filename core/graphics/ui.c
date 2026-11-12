/* ui.c - Silver Diamond Game Engine :: 2D UI renderer (shader only)
 *
 * One program draws everything: rounded rectangles are evaluated as signed
 * distance fields per fragment, text is one quad per glyph sampled from a
 * font atlas that is generated at runtime (5x7 cells, classic 5x7 font).
 * The CPU only fills a vertex buffer - all shading happens on the GPU.
 */
#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef SDGE_NO_GL

#include <glad/glad.h>

/* ------------------------------------------------------------- shaders */

static const char *UI_VS =
"#version 330 core\n"
"layout(location=0) in vec2 aPos;\n"     /* pixel position          */
"layout(location=1) in vec2 aUV;\n"      /* atlas uv                */
"layout(location=2) in vec4 aColor;\n"   /* fill colour             */
"layout(location=3) in vec4 aEdge;\n"    /* rgb border, a = alpha   */
"layout(location=4) in vec4 aShape;\n"   /* x radius, y softness,   */
                                         /* z mode, w border width  */
"layout(location=5) in vec4 aLocal;\n"   /* xy local px, zw half px */
"uniform mat4 uProj;\n"
"out vec2 vUV;\n"
"out vec4 vColor;\n"
"out vec4 vEdge;\n"
"out vec4 vShape;\n"
"out vec4 vLocal;\n"
"void main() {\n"
"    vUV = aUV;\n"
"    vColor = aColor;\n"
"    vEdge = aEdge;\n"
"    vShape = aShape;\n"
"    vLocal = aLocal;\n"
"    gl_Position = uProj * vec4(aPos, 0.0, 1.0);\n"
"}\n";

static const char *UI_FS =
"#version 330 core\n"
"in vec2 vUV;\n"
"in vec4 vColor;\n"
"in vec4 vEdge;\n"
"in vec4 vShape;\n"
"in vec4 vLocal;\n"
"uniform sampler2D uAtlas;\n"
"out vec4 frag;\n"
"float sdRoundBox(vec2 p, vec2 b, float r) {\n"
"    r = min(r, min(b.x, b.y));\n"
"    vec2 q = abs(p) - b + r;\n"
"    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;\n"
"}\n"
"void main() {\n"
"    float mode = vShape.z;\n"
"    vec4 c;\n"
"    if (mode > 0.5 && mode < 1.5) {\n"
"        /* text: the atlas stores the glyph coverage */\n"
"        float a = texture(uAtlas, vUV).r;\n"
"        c = vec4(vColor.rgb, vColor.a * a);\n"
"    } else if (mode > 1.5) {\n"
"        /* soft blob - used for shadows */\n"
"        float d = sdRoundBox(vLocal.xy, vLocal.zw, vShape.x);\n"
"        float s = max(vShape.y, 0.001);\n"
"        float a = 1.0 - smoothstep(-s, s, d);\n"
"        c = vec4(vColor.rgb, vColor.a * a);\n"
"    } else {\n"
"        vec2 half_size = vLocal.zw;\n"
"        float r = vShape.x;\n"
"        float bw = vShape.w;\n"
"        float d = sdRoundBox(vLocal.xy, half_size, r);\n"
"        float aa = fwidth(d) + 1e-4;\n"
"        /* the shape itself and the inner (fill) area */\n"
"        float outer = 1.0 - smoothstep(-aa, aa, d);\n"
"        vec2 ihalf = max(half_size - vec2(bw), vec2(0.0));\n"
"        float di = sdRoundBox(vLocal.xy, ihalf, max(r - bw, 0.0));\n"
"        float inner = 1.0 - smoothstep(-aa, aa, di);\n"
"        /* the border is the ring between the two */\n"
"        float border = (bw > 0.0) ? clamp(outer - inner, 0.0, 1.0) : 0.0;\n"
"        vec3 rgb = mix(vColor.rgb, vEdge.rgb, border);\n"
"        float a = max(inner * vColor.a, border * vEdge.a);\n"
"        c = vec4(rgb, a);\n"
"    }\n"
"    if (c.a <= 0.002) discard;\n"
"    frag = c;\n"
"}\n";

/* --------------------------------------------------------------- state */

typedef struct {
    int      inited;
    unsigned prog;
    unsigned vao, vbo;
    unsigned atlas;
    int      loc_proj;
    int      loc_atlas;

    sdga_font *font;        /* the current font (not owned) */
    sdga_font *builtin;     /* the built-in bitmap font (owned) */

    float   *verts;          /* CPU staging buffer */
    int      vert_count;     /* used vertices */
    int      vert_cap;       /* capacity in vertices */
    int      quad_count;

    float    proj[16];
    int      width, height;
} ui_state;

static ui_state g_ui;

/* the current font - the built-in bitmap font is created lazily so that
 * the text metrics also work without a GL context */
static sdga_font *ui_current_font(void) {
    if (!g_ui.font) {
        if (!g_ui.builtin) g_ui.builtin = sdga_load_builtin_font(14.0f);
        g_ui.font = g_ui.builtin;
    }
    return g_ui.font;
}

/* --------------------------------------------------------- font atlas */

/* uploads the coverage atlas of a font into the texture */
static int ui_upload_atlas(const unsigned char *pixels, int w, int h) {
    glGenTextures(1, &g_ui.atlas);
    glBindTexture(GL_TEXTURE_2D, g_ui.atlas);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED,
                 GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return 0;
}

static int ui_apply_font(sdga_font *f) {
    if (!f) return -1;
    const unsigned char *pixels = sdga_font_pixels(f);
    int w = 0, h = 0;
    if (!pixels || sdga_font_texture_size(f, &w, &h) != 0 || w <= 0 || h <= 0)
        return -1;
    if (g_ui.atlas) {
        glDeleteTextures(1, &g_ui.atlas);
        g_ui.atlas = 0;
    }
    ui_upload_atlas(pixels, w, h);
    g_ui.font = f;
    return 0;
}

/* ------------------------------------------------------------- helpers */

static unsigned ui_compile(GLenum type, const char *src) {
    unsigned s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    int ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        GLsizei n = 0;
        glGetShaderInfoLog(s, sizeof(log), &n, log);
        fprintf(stderr, "ui: shader compile failed:\n%.*s\n", (int)n, log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static int ui_ensure_verts(int need) {
    if (need <= g_ui.vert_cap) return 0;
    int cap = g_ui.vert_cap ? g_ui.vert_cap : 2048;
    while (cap < need) cap *= 2;
    float *nv = (float *)realloc(g_ui.verts, sizeof(float) * (size_t)cap * 20u);
    if (!nv) return -1;
    g_ui.verts = nv;
    g_ui.vert_cap = cap;
    return 0;
}

/* appends one quad (2 triangles, no index buffer) */
static void ui_push_quad(float x, float y, float w, float h,
                         float u0, float v0, float u1, float v1,
                         const float color[4], const float border[4],
                         float radius, float bw, float softness, int mode) {
    if (w <= 0.0f || h <= 0.0f) return;
    if (ui_ensure_verts(g_ui.vert_count + 6) != 0) return;

    const float hw = w * 0.5f, hh = h * 0.5f;

    /* x, y, u, v, rgba, edge rgba, shape, local */
    const float pos[6][2] = {
        { x,      y      }, { x + w,  y      }, { x + w,  y + h },
        { x,      y      }, { x + w,  y + h }, { x,      y + h },
    };
    const float loc[6][2] = {
        { -hw, -hh }, { hw, -hh }, { hw, hh },
        { -hw, -hh }, { hw,  hh }, { -hw, hh },
    };
    const float uv[6][2] = {
        { u0, v0 }, { u1, v0 }, { u1, v1 },
        { u0, v0 }, { u1, v1 }, { u0, v1 },
    };

    float *p = g_ui.verts + (size_t)g_ui.vert_count * 20u;
    for (int i = 0; i < 6; ++i) {
        /* layout (20 floats): pos(2) uv(2) colour(4) border rgb(3)+width(1)
         *                        shape(4) local(2)+half(2) */
        *p++ = pos[i][0];  *p++ = pos[i][1];
        *p++ = uv[i][0];   *p++ = uv[i][1];
        *p++ = color[0];   *p++ = color[1];
        *p++ = color[2];   *p++ = color[3];
        *p++ = border[0];  *p++ = border[1];
        *p++ = border[2];  *p++ = border[3];
        *p++ = radius;     *p++ = softness;
        *p++ = (float)mode; *p++ = bw;
        *p++ = loc[i][0];   *p++ = loc[i][1];
        *p++ = hw;          *p++ = hh;
    }
    g_ui.vert_count += 6;
    g_ui.quad_count++;
}

/* --------------------------------------------------------------- device */

SDGE_API int sdui_init(void) {
    if (g_ui.inited) return 0;

    unsigned vs = ui_compile(GL_VERTEX_SHADER, UI_VS);
    unsigned fs = ui_compile(GL_FRAGMENT_SHADER, UI_FS);
    if (!vs || !fs) return -1;
    unsigned prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    int ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        GLsizei n = 0;
        glGetProgramInfoLog(prog, sizeof(log), &n, log);
        fprintf(stderr, "ui: program link failed:\n%.*s\n", (int)n, log);
        glDeleteProgram(prog);
        glDeleteShader(vs);
        glDeleteShader(fs);
        return -1;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);

    g_ui.prog = prog;
    g_ui.loc_proj = glGetUniformLocation(prog, "uProj");
    g_ui.loc_atlas = glGetUniformLocation(prog, "uAtlas");

    /* vertex layout: 20 floats per vertex */
    glGenVertexArrays(1, &g_ui.vao);
    glBindVertexArray(g_ui.vao);
    glGenBuffers(1, &g_ui.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_ui.vbo);
    glBufferData(GL_ARRAY_BUFFER, (long long)sizeof(float) * 2048 * 20,
                 NULL, GL_DYNAMIC_DRAW);
    {
        const GLsizei stride = 20 * sizeof(float);
        const GLsizei offs[6] = { 0, 2, 4, 8, 12, 16 };
        const GLsizei comps[6] = { 2, 2, 4, 4, 4, 4 };
        for (int i = 0; i < 6; ++i) {
            glEnableVertexAttribArray((unsigned)i);
            glVertexAttribPointer((unsigned)i, comps[i], GL_FLOAT, GL_FALSE,
                                  stride,
                                  (void *)((size_t)offs[i] * sizeof(float)));
        }
    }
    glBindVertexArray(0);

    /* the built-in bitmap font is the default */
    g_ui.builtin = sdga_load_builtin_font(14.0f);
    if (!g_ui.builtin || ui_apply_font(g_ui.builtin) != 0) {
        fprintf(stderr, "ui: cannot create the built-in font\n");
        glDeleteProgram(prog);
        return -1;
    }
    g_ui.vert_count = 0;
    g_ui.quad_count = 0;
    g_ui.inited = 1;
    return 0;
}

SDGE_API int sdui_set_font(sdga_font *font) {
    if (!g_ui.inited) return -1;
    if (!font) font = g_ui.builtin;
    return ui_apply_font(font);
}

SDGE_API const sdga_font *sdui_get_font(void) { return ui_current_font(); }

SDGE_API void sdui_shutdown(void) {
    if (!g_ui.inited) return;
    glDeleteTextures(1, &g_ui.atlas);
    glDeleteBuffers(1, &g_ui.vbo);
    glDeleteVertexArrays(1, &g_ui.vao);
    glDeleteProgram(g_ui.prog);
    free(g_ui.verts);
    sdga_free_font(g_ui.builtin);
    memset(&g_ui, 0, sizeof(g_ui));
}

SDGE_API void sdui_begin(int width, int height) {
    if (!g_ui.inited) return;
    g_ui.width = width > 0 ? width : 1;
    g_ui.height = height > 0 ? height : 1;
    g_ui.vert_count = 0;
    g_ui.quad_count = 0;

    /* pixel space, origin top-left, y down */
    memset(g_ui.proj, 0, sizeof(g_ui.proj));
    g_ui.proj[0] = 2.0f / (float)g_ui.width;
    g_ui.proj[5] = -2.0f / (float)g_ui.height;
    g_ui.proj[10] = -1.0f;
    g_ui.proj[12] = -1.0f;
    g_ui.proj[13] = 1.0f;
    g_ui.proj[15] = 1.0f;

    /* the UI is an overlay: no depth, alpha blending */
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

SDGE_API void sdui_end(void) {
    if (!g_ui.inited || g_ui.vert_count == 0) return;

    glUseProgram(g_ui.prog);
    glUniformMatrix4fv(g_ui.loc_proj, 1, GL_FALSE, g_ui.proj);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_ui.atlas);
    if (g_ui.loc_atlas >= 0) glUniform1i(g_ui.loc_atlas, 0);

    glBindVertexArray(g_ui.vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_ui.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (long long)sizeof(float) * (size_t)g_ui.vert_count * 20u,
                 g_ui.verts, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, g_ui.vert_count);
    glBindVertexArray(0);
}

SDGE_API int sdui_quad_count(void) { return g_ui.quad_count; }

/* ---------------------------------------------------------------- draw */

SDGE_API void sdui_draw_rect(const sdui_rect *r) {
    if (!r || !g_ui.inited) return;
    const float zero[4] = { 0, 0, 0, 0 };
    ui_push_quad(r->x, r->y, r->w, r->h, 0, 0, 0, 0,
                 r->color, r->border_color,
                 r->radius, r->border_width, r->softness,
                 r->mode ? r->mode : SDUI_MODE_BOX);
    (void)zero;
}

SDGE_API void sdui_box(float x, float y, float w, float h,
                       const float color[4]) {
    if (!g_ui.inited) return;
    const float zero[4] = { 0, 0, 0, 0 };
    ui_push_quad(x, y, w, h, 0, 0, 0, 0, color, zero, 0, 0, 0, SDUI_MODE_BOX);
}

/* --------------------------------------------------------------- text */

SDGE_API float sdui_text(float x, float y, const char *utf8, float size,
                          const float color[4]) {
    if (!utf8 || !g_ui.inited || size <= 0.0f) return 0.0f;
    sdga_font *f = ui_current_font();
    if (!f) return 0.0f;

    /* the font holds one rasterization per used size - the closest one is
     * used, so the bitmaps are never scaled down (the thin strokes of a
     * small text would disappear) */
    int face = sdga_font_face_for(f, size);
    float native = sdga_font_face_size(f, face);
    float k = (native > 0.0f) ? (size / native) : 1.0f;
    float line_ascent = sdga_font_ascent(f) * k;

    const float zero[4] = { 0, 0, 0, 0 };
    float pen = x;
    for (const unsigned char *p = (const unsigned char *)utf8; *p; ++p) {
        unsigned ch = *p;
        if (ch == '\n') {
            pen = x;
            y += sdga_font_line_height(f) * k;
            continue;
        }
        const sdga_glyph *g = sdga_font_face_glyph(f, face, ch);
        if (!g) continue;
        if (g->w > 0 && g->h > 0) {
            float gw = (float)g->w * k;
            float gh = (float)g->h * k;
            float gx = pen + (float)g->bearing_x * k;
            float gy = y + line_ascent - (float)g->bearing_y * k;
            ui_push_quad(gx, gy, gw, gh, g->u0, g->v0, g->u1, g->v1,
                         color, zero, 0, 0, 0, SDUI_MODE_TEXT);
        }
        pen += g->advance * k;
    }
    return pen - x;
}

SDGE_API float sdui_text_centered(float x, float y, float w, float h,
                                  const char *utf8, float size,
                                  const float color[4]) {
    float tw = sdui_text_width(utf8, size);
    float th = sdui_text_height(size);
    return sdui_text(x + (w - tw) * 0.5f, y + (h - th) * 0.5f, utf8, size,
                     color);
}

SDGE_API float sdui_text_width(const char *utf8, float size) {
    if (!utf8 || size <= 0.0f) return 0.0f;
    sdga_font *f = ui_current_font();
    if (!f) return 0.0f;
    int face = sdga_font_face_for(f, size);
    float native = sdga_font_face_size(f, face);
    float k = (native > 0.0f) ? (size / native) : 1.0f;
    float w = 0.0f;
    for (const unsigned char *p = (const unsigned char *)utf8; *p; ++p) {
        if (*p == '\n') continue;
        const sdga_glyph *g = sdga_font_face_glyph(f, face, *p);
        if (g) w += g->advance * k;
    }
    return w;
}

SDGE_API float sdui_text_height(float size) {
    if (size <= 0.0f) return 0.0f;
    sdga_font *f = ui_current_font();
    if (!f) return 0.0f;
    int face = sdga_font_face_for(f, size);
    float native = sdga_font_face_size(f, face);
    float k = (native > 0.0f) ? (size / native) : 1.0f;
    return sdga_font_line_height(f) * k;
}

#else /* SDGE_NO_GL */

SDGE_API int  sdui_init(void) { return -1; }
SDGE_API void sdui_shutdown(void) { }
SDGE_API void sdui_begin(int w, int h) { (void)w; (void)h; }
SDGE_API void sdui_end(void) { }
SDGE_API int  sdui_quad_count(void) { return 0; }
SDGE_API void sdui_draw_rect(const sdui_rect *r) { (void)r; }
SDGE_API void sdui_box(float x, float y, float w, float h,
                        const float c[4]) { (void)x; (void)y; (void)w; (void)h; (void)c; }
SDGE_API float sdui_text(float x, float y, const char *s, float size,
                          const float c[4]) { (void)x; (void)y; (void)s; (void)size; (void)c; return 0; }
SDGE_API float sdui_text_centered(float x, float y, float w, float h,
                                   const char *s, float size, const float c[4]) {
    (void)x; (void)y; (void)w; (void)h; (void)s; (void)size; (void)c; return 0;
}
SDGE_API float sdui_text_width(const char *s, float size) { (void)s; (void)size; return 0; }
SDGE_API float sdui_text_height(float size) { (void)size; return 0; }

#endif /* SDGE_NO_GL */
