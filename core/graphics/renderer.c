/* renderer.c - Silver Diamond Game Engine :: forward renderer implementation
 *
 * glad/GL are a PRIVATE dependency of this TU: the engine DLL loads GL
 * itself, applications only see the sdgr_* API.
 */
#include "renderer.h"
#include "shader_lang.h"
#include "../assets/assets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#ifndef SDGE_NO_GL

#include <glad/glad.h>

#define SDGR_MAX_JOINTS_STR "128"

static const float *r_identity(void) {
    static float m[16];
    static int init = 0;
    if (!init) {
        m4_identity(m);
        init = 1;
    }
    return m;
}

/* ------------------------------------------------------------- shaders */
static const char *R_VS =
"#version 330 core\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec3 aNrm;\n"
"layout(location=2) in vec4 aJoint;\n"
"layout(location=3) in vec4 aWeight;\n"
"layout(location=4) in vec2 aUV;\n"
"layout(location=5) in vec2 aUV1;\n"
"layout(location=6) in vec4 aTan;\n"
"uniform mat4 uProj, uView, uModel;\n"
"uniform mat4 uJoints[" SDGR_MAX_JOINTS_STR "];\n"
"uniform int uSkinned;\n"
"out vec3 vN;\n"
"out vec3 vWpos;\n"
"out vec2 vUV;\n"
"out vec2 vUV1;\n"
"out vec4 vTan;\n"
"void main() {\n"
"    mat4 skin = mat4(1.0);\n"
"    if (uSkinned == 1) {\n"
"        skin = uJoints[int(aJoint.x)] * aWeight.x\n"
"             + uJoints[int(aJoint.y)] * aWeight.y\n"
"             + uJoints[int(aJoint.z)] * aWeight.z\n"
"             + uJoints[int(aJoint.w)] * aWeight.w;\n"
"    }\n"
"    mat3 nm = mat3(uModel) * mat3(skin);\n"
"    vec4 world = uModel * skin * vec4(aPos, 1.0);\n"
"    vWpos = world.xyz;\n"
"    vN = nm * aNrm;\n"
"    vUV = aUV;\n"
"    vUV1 = aUV1;\n"
"    vec3 t = nm * aTan.xyz;\n"
"    float tl = length(t);\n"
"    vTan = vec4(tl > 1e-6 ? t / tl : vec3(1.0, 0.0, 0.0), aTan.w);\n"
"    gl_Position = uProj * uView * world;\n"
"}\n";

static const char *R_FS =
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
"uniform mat3 uUVXform[" "5" "];\n"
"uniform int uUVSet[" "5" "];\n"
"uniform vec3 uCamPos;\n"
"uniform vec3 uAmbient;\n"
"uniform vec3 uSunDir, uSunColor;\n"
"uniform float uSunI;\n"
"uniform vec4 uPointPos[" "8" "];\n"
"uniform vec4 uPointCol[" "8" "];\n"
"uniform int uPointCount;\n"
"uniform vec4 uBaseColor;\n"
"uniform float uMetallic, uRoughness;\n"
"uniform vec3 uEmissive;\n"
"uniform float uNormalScale, uOcclusionStrength, uAlphaCutoff;\n"
"uniform int uDoubleSided, uAlphaMode;\n"
"out vec4 frag;\n"
"float ggx(float ndh, float a) {\n"
"    float a2 = a * a;\n"
"    float d = ndh * ndh * (a2 - 1.0) + 1.0;\n"
"    return a2 / (3.14159265 * d * d);\n"
"}\n"
"vec2 slotUV(int slot) {\n"
"    vec2 uv = (uUVSet[slot] == 1) ? vUV1 : vUV;\n"
"    return (uUVXform[slot] * vec3(uv, 1.0)).xy;\n"
"}\n"
"mat3 cotangentFrame(vec3 N, vec3 p, vec2 uv) {\n"
"    vec3 dp1 = dFdx(p), dp2 = dFdy(p);\n"
"    vec2 duv1 = dFdx(uv), duv2 = dFdy(uv);\n"
"    vec3 dp2perp = cross(dp2, N);\n"
"    vec3 dp1perp = cross(N, dp1);\n"
"    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;\n"
"    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;\n"
"    float invmax = inversesqrt(max(dot(T, T), dot(B, B)));\n"
"    return mat3(T * invmax, B * invmax, N);\n"
"}\n"
"void main() {\n"
"    vec4 base = uBaseColor;\n"
"    if (uHasBase == 1) base *= texture(uBaseTex, slotUV(0));\n"
"    if (uAlphaMode == 1) {\n"
"        if (base.a < uAlphaCutoff) discard;\n"
"    } else if (uAlphaMode == 2) {\n"
"        if (base.a <= 0.0) discard;\n"
"    }\n"
"    float metallic = uMetallic;\n"
"    float roughness = uRoughness;\n"
"    if (uHasMR == 1) {\n"
"        vec4 mr = texture(uMRTex, slotUV(1));\n"
"        roughness *= mr.g;\n"
"        metallic *= mr.b;\n"
"    }\n"
"    float ao = 1.0;\n"
"    if (uHasOcclusion == 1)\n"
"        ao = mix(1.0, texture(uOcclusionTex, slotUV(3)).r, uOcclusionStrength);\n"
"    vec3 N = normalize(vN);\n"
"    if (uDoubleSided == 1 && !gl_FrontFacing) N = -N;\n"
"    if (uHasNormal == 1) {\n"
"        vec2 nuv = slotUV(2);\n"
"        vec3 nm = texture(uNormalTex, nuv).rgb * 2.0 - 1.0;\n"
"        nm.xy *= uNormalScale;\n"
"        mat3 tbn;\n"
"        if (uHasTangent == 1) {\n"
"            vec3 T = normalize(vTan.xyz - N * dot(N, vTan.xyz));\n"
"            vec3 B = cross(N, T) * (vTan.w < 0.0 ? -1.0 : 1.0);\n"
"            tbn = mat3(T, B, N);\n"
"        } else {\n"
"            tbn = cotangentFrame(N, vWpos, nuv);\n"
"        }\n"
"        N = normalize(tbn * nm);\n"
"    }\n"
"    vec3 V = normalize(uCamPos - vWpos);\n"
"    vec3 diffuse = base.rgb * (1.0 - metallic);\n"
"    vec3 f0 = mix(vec3(0.04), base.rgb, metallic);\n"
"    float a = roughness * roughness + 1e-3;\n"
"    vec3 col = uAmbient * base.rgb * ao;\n"
"    if (uSunI > 0.0) {\n"
"        vec3 L = normalize(-uSunDir);\n"
"        float ndl = max(dot(N, L), 0.0);\n"
"        vec3 H = normalize(L + V);\n"
"        float spec = ggx(max(dot(N, H), 0.0), a);\n"
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
"        float spec = ggx(max(dot(N, H), 0.0), a);\n"
"        col += uPointCol[i].rgb * uPointCol[i].w * att * ndl\n"
"             * (diffuse + f0 * spec);\n"
"    }\n"
"    vec3 emis = uEmissive;\n"
"    if (uHasEmissive == 1) emis *= texture(uEmissiveTex, slotUV(4)).rgb;\n"
"    col += emis;\n"
"    float outA = (uAlphaMode == 2) ? base.a : 1.0;\n"
"    frag = vec4(col, outA);\n"
"}\n";

static const char *R_LINE_VS =
"#version 330 core\n"
"layout(location=0) in vec3 aPos;\n"
"uniform mat4 uProj, uView;\n"
"void main() {\n"
"    gl_Position = uProj * uView * vec4(aPos, 1.0);\n"
"}\n";

static const char *R_LINE_FS =
"#version 330 core\n"
"uniform vec4 uColor;\n"
"out vec4 frag;\n"
"void main() {\n"
"    frag = uColor;\n"
"}\n";

typedef struct {
    unsigned prog;
    int loc_proj, loc_view, loc_color;
    unsigned vbo;
    int    vbo_cap;
} r_lines;

/* Uniform locations shared by the built-in PBR program and every custom
 * shader program.  Programme with unused uniforms simply have -1 there. */
typedef struct {
    int proj, view, model, joints, skinned;
    int cam, ambient, sundir, suncol, suni;
    int ppos, pcol, pcount;
    int base, metal, rough, dbl;
    int emissive, normal_scale, occl_strength, alpha_cutoff;
    int alpha_mode, has_tangent;
    int tex[5];                 /* uBaseTex, uMRTex, ... */
    int has_tex[5];             /* uHasBase, uHasMR, ... */
    int uvxform, uvset;
    int time, resolution;
} r_locs;

typedef struct {
    unsigned prog;
    r_locs loc;

    float view[16], proj[16], cam_pos[3];
    float frustum[6][4];
    int   point_count;
    int   width, height;

    /* light state, re-uploaded to every program (built-in and custom) */
    float ambient[3];
    float sun_dir[3], sun_col[3];
    float sun_i;
    float point_pos[SDGR_MAX_POINT_LIGHTS][4];
    float point_col[SDGR_MAX_POINT_LIGHTS][4];

    r_lines lines;
} r_state;

static r_state g_r;
static int g_r_inited;

struct sdgr_shader {
    unsigned prog;
    int      valid;
    r_locs   loc;

    sdsl_uniform uniforms[SDSL_MAX_UNIFORMS];
    int          uniform_count;
    int          uniform_loc[SDSL_MAX_UNIFORMS];
    unsigned     uniform_tex[SDSL_MAX_UNIFORMS];
    int          uniform_unit[SDSL_MAX_UNIFORMS];
    int          sampler_count;

    float time_value;

    char *vs_src;
    char *fs_src;
};

static void r_locs_reset(r_locs *l) {
    memset(l, 0, sizeof(*l));
    l->proj = l->view = l->model = l->joints = l->skinned = -1;
    l->cam = l->ambient = l->sundir = l->suncol = l->suni = -1;
    l->ppos = l->pcol = l->pcount = -1;
    l->base = l->metal = l->rough = l->dbl = -1;
    l->emissive = l->normal_scale = l->occl_strength = l->alpha_cutoff = -1;
    l->alpha_mode = l->has_tangent = -1;
    for (int i = 0; i < 5; ++i) { l->tex[i] = -1; l->has_tex[i] = -1; }
    l->uvxform = l->uvset = -1;
    l->time = l->resolution = -1;
}

static void r_locs_get(unsigned prog, r_locs *l) {
    r_locs_reset(l);
    l->proj    = glGetUniformLocation(prog, "uProj");
    l->view    = glGetUniformLocation(prog, "uView");
    l->model   = glGetUniformLocation(prog, "uModel");
    l->joints  = glGetUniformLocation(prog, "uJoints");
    l->skinned = glGetUniformLocation(prog, "uSkinned");
    l->cam     = glGetUniformLocation(prog, "uCamPos");
    l->ambient = glGetUniformLocation(prog, "uAmbient");
    l->sundir  = glGetUniformLocation(prog, "uSunDir");
    l->suncol  = glGetUniformLocation(prog, "uSunColor");
    l->suni    = glGetUniformLocation(prog, "uSunI");
    l->ppos    = glGetUniformLocation(prog, "uPointPos[0]");
    l->pcol    = glGetUniformLocation(prog, "uPointCol[0]");
    l->pcount  = glGetUniformLocation(prog, "uPointCount");
    l->base    = glGetUniformLocation(prog, "uBaseColor");
    l->metal   = glGetUniformLocation(prog, "uMetallic");
    l->rough   = glGetUniformLocation(prog, "uRoughness");
    l->dbl     = glGetUniformLocation(prog, "uDoubleSided");
    l->emissive= glGetUniformLocation(prog, "uEmissive");
    l->normal_scale = glGetUniformLocation(prog, "uNormalScale");
    l->occl_strength = glGetUniformLocation(prog, "uOcclusionStrength");
    l->alpha_cutoff = glGetUniformLocation(prog, "uAlphaCutoff");
    l->alpha_mode = glGetUniformLocation(prog, "uAlphaMode");
    l->has_tangent = glGetUniformLocation(prog, "uHasTangent");
    {
        static const char *tex_names[5] = {
            "uBaseTex", "uMRTex", "uNormalTex", "uOcclusionTex", "uEmissiveTex"
        };
        static const char *has_names[5] = {
            "uHasBase", "uHasMR", "uHasNormal", "uHasOcclusion", "uHasEmissive"
        };
        for (int i = 0; i < 5; ++i) {
            l->tex[i] = glGetUniformLocation(prog, tex_names[i]);
            l->has_tex[i] = glGetUniformLocation(prog, has_names[i]);
        }
    }
    l->uvxform = glGetUniformLocation(prog, "uUVXform[0]");
    l->uvset   = glGetUniformLocation(prog, "uUVSet[0]");
    l->time    = glGetUniformLocation(prog, "uTime");
    l->resolution = glGetUniformLocation(prog, "uResolution");
}

/* Uploads the current light state to whatever program is bound.  Custom
 * shaders use the same uniforms as the built-in PBR program, so without this
 * their pbrLighting() call would always see black. */
static void r_apply_lights(const r_locs *l) {
    if (l->ambient >= 0) glUniform3fv(l->ambient, 1, g_r.ambient);
    if (l->sundir  >= 0) glUniform3fv(l->sundir, 1, g_r.sun_dir);
    if (l->suncol  >= 0) glUniform3fv(l->suncol, 1, g_r.sun_col);
    if (l->suni    >= 0) glUniform1f(l->suni, g_r.sun_i);
    if (l->pcount  >= 0) glUniform1i(l->pcount, g_r.point_count);
    for (int i = 0; i < g_r.point_count && i < SDGR_MAX_POINT_LIGHTS; ++i) {
        if (l->ppos >= 0) glUniform4fv(l->ppos + i, 1, g_r.point_pos[i]);
        if (l->pcol >= 0) glUniform4fv(l->pcol + i, 1, g_r.point_col[i]);
    }
}


static unsigned r_compile_log(unsigned type, const char *src,
                              char *log, int log_size) {
    if (log && log_size > 0) log[0] = '\0';
    unsigned s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    int ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char tmp[2048];
        GLsizei n = 0;
        glGetShaderInfoLog(s, sizeof(tmp), &n, tmp);
        if (log && log_size > 0)
            snprintf(log, (size_t)log_size, "%.*s", (int)n, tmp);
        else
            fprintf(stderr, "renderer: shader compile failed:\n%.*s\n",
                    (int)n, tmp);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static unsigned r_compile(unsigned type, const char *src) {
    return r_compile_log(type, src, NULL, 0);
}

static unsigned r_link_log(unsigned vs, unsigned fs,
                           char *log, int log_size) {
    if (log && log_size > 0) log[0] = '\0';
    unsigned prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    int ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char tmp[2048];
        GLsizei n = 0;
        glGetProgramInfoLog(prog, sizeof(tmp), &n, tmp);
        if (log && log_size > 0)
            snprintf(log, (size_t)log_size, "%.*s", (int)n, tmp);
        else
            fprintf(stderr, "renderer: program link failed:\n%.*s\n",
                    (int)n, tmp);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}


SDGE_API int sdgr_init(void) {
    if (g_r_inited) return 0;
    if (!gladLoadGL()) {
        fprintf(stderr, "renderer: gladLoadGL failed\n");
        return -1;
    }

    unsigned vs = r_compile(GL_VERTEX_SHADER, R_VS);
    unsigned fs = r_compile(GL_FRAGMENT_SHADER, R_FS);
    if (!vs || !fs) return -1;

    unsigned prog = r_link_log(vs, fs, NULL, 0);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!prog) return -1;

    memset(&g_r, 0, sizeof(g_r));
    g_r.prog = prog;
    r_locs_get(prog, &g_r.loc);

    /* the line program (the gizmos of the scene editor) */
    {
        unsigned vs = r_compile(GL_VERTEX_SHADER, R_LINE_VS);
        unsigned fs = r_compile(GL_FRAGMENT_SHADER, R_LINE_FS);
        if (vs && fs) {
            unsigned lp = glCreateProgram();
            glAttachShader(lp, vs);
            glAttachShader(lp, fs);
            glLinkProgram(lp);
            int lok = 0;
            glGetProgramiv(lp, GL_LINK_STATUS, &lok);
            if (lok) {
                g_r.lines.prog = lp;
                g_r.lines.loc_proj = glGetUniformLocation(lp, "uProj");
                g_r.lines.loc_view = glGetUniformLocation(lp, "uView");
                g_r.lines.loc_color = glGetUniformLocation(lp, "uColor");
                glGenBuffers(1, &g_r.lines.vbo);
                g_r.lines.vbo_cap = 0;
            } else {
                glDeleteProgram(lp);
            }
        }
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
    }

    m4_identity(g_r.view);
    m4_identity(g_r.proj);
    g_r.point_count = 0;
    g_r.width = 1;
    g_r.height = 1;
    g_r_inited = 1;
    sdgr_set_ambient(0.2f, 0.2f, 0.25f);
    {
        float dir[3] = { -0.5f, -1.0f, -0.3f };
        float col[3] = { 1.0f, 1.0f, 1.0f };
        sdgr_set_sun(dir, col, 2.0f);
    }
    return 0;
}

SDGE_API void sdgr_shutdown(void) {
    if (!g_r_inited) return;
    if (g_r.lines.prog) glDeleteProgram(g_r.lines.prog);
    if (g_r.lines.vbo) glDeleteBuffers(1, &g_r.lines.vbo);
    glDeleteProgram(g_r.prog);
    memset(&g_r, 0, sizeof(g_r));
    g_r_inited = 0;
}

SDGE_API void sdgr_resize(int width, int height) {
    if (!g_r_inited) return;
    g_r.width  = width  > 0 ? width  : 1;
    g_r.height = height > 0 ? height : 1;
    glViewport(0, 0, g_r.width, g_r.height);
}

SDGE_API void sdgr_begin_frame(const float clear_color[4]) {
    if (!g_r_inited) return;
    float c[4] = { 0.09f, 0.10f, 0.14f, 1.0f };
    if (clear_color) memcpy(c, clear_color, sizeof(c));
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glClearColor(c[0], c[1], c[2], c[3]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(g_r.prog);
    g_r.point_count = 0;
    glUniform1i(g_r.loc.pcount, 0);
    for (int i = 0; i < SDGR_TEX_SLOTS; ++i)
        if (g_r.loc.has_tex[i] >= 0) glUniform1i(g_r.loc.has_tex[i], 0);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
}

SDGE_API void sdgr_end_frame(void) {
}

SDGE_API void sdgr_set_camera(const sdg_mat4 view, const sdg_mat4 proj,
                              const float cam_pos[3]) {
    if (!g_r_inited) return;
    m4_copy(g_r.view, view);
    m4_copy(g_r.proj, proj);
    if (cam_pos) memcpy(g_r.cam_pos, cam_pos, sizeof(float) * 3);
    sdg_mat4 vp;
    m4_mul(vp, g_r.proj, g_r.view);
    frustum_from_vp(g_r.frustum, vp);
}

SDGE_API int sdgr_sphere_visible(const float center[3], float radius) {
    if (!g_r_inited || !center) return 1;
    return sphere_in_frustum(g_r.frustum, center, radius);
}

SDGE_API void sdgr_set_ambient(float r, float g, float b) {
    if (!g_r_inited) return;
    g_r.ambient[0] = r; g_r.ambient[1] = g; g_r.ambient[2] = b;
    glUseProgram(g_r.prog);
    r_apply_lights(&g_r.loc);
}

SDGE_API void sdgr_set_sun(const float dir[3], const float color[3],
                           float intensity) {
    if (!g_r_inited) return;
    float d[3] = { 0, -1, 0 }, c[3] = { 1, 1, 1 };
    if (dir)   memcpy(d, dir, sizeof(float) * 3);
    if (color) memcpy(c, color, sizeof(float) * 3);
    float len = sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
    if (len > 1e-9f) { d[0] /= len; d[1] /= len; d[2] /= len; }
    memcpy(g_r.sun_dir, d, sizeof(d));
    memcpy(g_r.sun_col, c, sizeof(c));
    g_r.sun_i = intensity;
    glUseProgram(g_r.prog);
    r_apply_lights(&g_r.loc);
}

SDGE_API void sdgr_set_point_light(int index, const float pos[3],
                                   const float color[3],
                                   float intensity, float radius) {
    if (!g_r_inited) return;
    if (index < 0 || index >= SDGR_MAX_POINT_LIGHTS) return;
    float *p = g_r.point_pos[index];
    float *c = g_r.point_col[index];
    p[0] = pos ? pos[0] : 0; p[1] = pos ? pos[1] : 0;
    p[2] = pos ? pos[2] : 0; p[3] = radius > 0.0f ? radius : 1.0f;
    c[0] = color ? color[0] : 1; c[1] = color ? color[1] : 1;
    c[2] = color ? color[2] : 1; c[3] = intensity;
    if (index + 1 > g_r.point_count) g_r.point_count = index + 1;
    glUseProgram(g_r.prog);
    r_apply_lights(&g_r.loc);
}

SDGE_API unsigned sdgr_upload_texture(int w, int h,
                                       const unsigned char *rgba8) {
    sdgr_texture_desc d;
    memset(&d, 0, sizeof(d));
    d.srgb = 0;
    d.wrap_s = d.wrap_t = GL_REPEAT;
    d.min_filter = GL_LINEAR_MIPMAP_LINEAR;
    d.mag_filter = GL_LINEAR;
    d.mipmaps = 1;
    return sdgr_upload_texture_ex(w, h, rgba8, &d);
}

SDGE_API unsigned sdgr_upload_texture_ex(int w, int h,
                                          const unsigned char *rgba8,
                                          const sdgr_texture_desc *desc) {
    if (!g_r_inited || !rgba8 || w <= 0 || h <= 0) return 0;

    int srgb = desc ? desc->srgb : 0;
    int wrap_s = (desc && desc->wrap_s) ? desc->wrap_s : GL_REPEAT;
    int wrap_t = (desc && desc->wrap_t) ? desc->wrap_t : GL_REPEAT;
    int min_filter = (desc && desc->min_filter) ? desc->min_filter
                                                 : GL_LINEAR_MIPMAP_LINEAR;
    int mag_filter = (desc && desc->mag_filter) ? desc->mag_filter : GL_LINEAR;
    int mipmaps = desc ? desc->mipmaps : 1;

    unsigned tex = 0;
    glGenTextures(1, &tex);
    if (!tex) return 0;
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    /* sRGB textures are decoded by the sampler, so the shader always works
     * in linear space (glTF: base color + emissive are sRGB encoded) */
    glTexImage2D(GL_TEXTURE_2D, 0, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8,
                 w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba8);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_s);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag_filter);
    if (mipmaps) {
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter);
    } else {
        int f = min_filter;
        /* a min filter that needs mipmaps but has none -> fall back */
        if (f == GL_LINEAR_MIPMAP_LINEAR || f == GL_LINEAR_MIPMAP_NEAREST)
            f = GL_LINEAR;
        else if (f == GL_NEAREST_MIPMAP_LINEAR || f == GL_NEAREST_MIPMAP_NEAREST)
            f = GL_NEAREST;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

SDGE_API void sdgr_free_texture(unsigned tex) {
    if (tex) glDeleteTextures(1, &tex);
}

SDGE_API void sdgr_get_camera_pos(float out_pos[3]) {
    if (!out_pos) return;
    memcpy(out_pos, g_r.cam_pos, sizeof(float) * 3);
}

/* builds the 3x3 UV transform of one texture slot:
 *   uv' = rotation * (uv * scale) + offset                            */
static void r_uv_matrix(float out[9], const sdgr_material *m, int slot) {
    float sx = m->uv_scale[slot][0];
    float sy = m->uv_scale[slot][1];
    float c = cosf(m->uv_rot[slot]);
    float s = sinf(m->uv_rot[slot]);
    out[0] =  c * sx;  out[1] = s * sx;  out[2] = 0.0f;
    out[3] = -s * sy;  out[4] = c * sy;  out[5] = 0.0f;
    out[6] = m->uv_offset[slot][0];
    out[7] = m->uv_offset[slot][1];
    out[8] = 1.0f;
}

static void r_draw_mesh_program(unsigned prog, const r_locs *loc,
                                const sdge_mesh *mesh, const sdg_mat4 model,
                                const float *joints, int joint_count,
                                const sdgr_material *mat,
                                const sdgr_texture_set *tex,
                                sdgr_shader *shader) {
    if (!g_r_inited || !mesh || !mesh->vao) return;

    int skinned = (mesh->has_skin && joints && joint_count > 0);
    if (skinned && joint_count > SDGR_MAX_JOINTS) joint_count = SDGR_MAX_JOINTS;

    glUseProgram(prog);
    r_apply_lights(loc);
    glUniformMatrix4fv(loc->proj, 1, GL_FALSE, g_r.proj);
    glUniformMatrix4fv(loc->view, 1, GL_FALSE, g_r.view);
    glUniformMatrix4fv(loc->model, 1, GL_FALSE,
                       model ? model : r_identity());
    glUniform1i(loc->skinned, skinned ? 1 : 0);
    if (skinned)
        glUniformMatrix4fv(loc->joints, joint_count, GL_FALSE, joints);
    glUniform3fv(loc->cam, 1, g_r.cam_pos);

    float bc[4] = { 0.8f, 0.8f, 0.8f, 1.0f };
    float em[3] = { 0.0f, 0.0f, 0.0f };
    float metallic = 0.0f, roughness = 0.6f, normal_scale = 1.0f;
    float occl = 1.0f, cutoff = 0.5f;
    int alpha_mode = SDGR_ALPHA_OPAQUE, double_sided = mesh->double_sided;

    if (mat) {
        memcpy(bc, mat->base_color, sizeof(bc));
        memcpy(em, mat->emissive, sizeof(em));
        metallic = mat->metallic;
        roughness = mat->roughness;
        normal_scale = mat->normal_scale > 0.0f ? mat->normal_scale : 1.0f;
        occl = mat->occlusion_strength;
        cutoff = mat->alpha_cutoff;
        alpha_mode = mat->alpha_mode;
        double_sided = double_sided || mat->double_sided;
    }
    glUniform4fv(loc->base, 1, bc);
    glUniform3fv(loc->emissive, 1, em);
    glUniform1f(loc->metal, metallic);
    glUniform1f(loc->rough, roughness < 0.045f ? 0.045f : roughness);
    glUniform1f(loc->normal_scale, normal_scale);
    glUniform1f(loc->occl_strength, occl);
    glUniform1f(loc->alpha_cutoff, cutoff);
    glUniform1i(loc->alpha_mode, alpha_mode);
    glUniform1i(loc->dbl, double_sided ? 1 : 0);
    glUniform1i(loc->has_tangent, 0);

    if (shader) {
        if (loc->time >= 0) glUniform1f(loc->time, shader->time_value);
        if (loc->resolution >= 0)
            glUniform2f(loc->resolution,
                        (float)(g_r.width > 0 ? g_r.width : 1),
                        (float)(g_r.height > 0 ? g_r.height : 1));
    }

    /* ---- textures: one sampler unit per slot ---- */
    unsigned texid[SDGR_TEX_SLOTS] = { 0, 0, 0, 0, 0 };
    if (tex) {
        texid[SDGR_SLOT_BASE]      = tex->base_color;
        texid[SDGR_SLOT_MR]        = tex->metallic_roughness;
        texid[SDGR_SLOT_NORMAL]    = tex->normal;
        texid[SDGR_SLOT_OCCLUSION] = tex->occlusion;
        texid[SDGR_SLOT_EMISSIVE]  = tex->emissive;
    }
    for (int i = 0; i < SDGR_TEX_SLOTS; ++i) {
        int has = (texid[i] != 0 && mesh->has_texcoords);
        if (i == SDGR_SLOT_NORMAL) has = 0;
        if (loc->has_tex[i] >= 0) glUniform1i(loc->has_tex[i], has);
        if (loc->tex[i] >= 0) {
            glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_2D, has ? texid[i] : 0);
            glUniform1i(loc->tex[i], i);
        }
    }

    /* ---- user sampler uniforms (custom shaders, units 5+) ---- */
    if (shader) {
        for (int i = 0; i < shader->uniform_count; ++i) {
            if (shader->uniforms[i].type != SDSL_UNIFORM_SAMPLER2D) continue;
            if (shader->uniform_loc[i] < 0) continue;
            glActiveTexture(GL_TEXTURE0 + (unsigned)shader->uniform_unit[i]);
            glBindTexture(GL_TEXTURE_2D, shader->uniform_tex[i]);
        }
        glActiveTexture(GL_TEXTURE0);
    }

    /* ---- UV sets and per-slot texture transforms ---- */
    {
        float xf[SDGR_TEX_SLOTS][9];
        int   sets[SDGR_TEX_SLOTS];
        for (int i = 0; i < SDGR_TEX_SLOTS; ++i) {
            if (mat) {
                r_uv_matrix(xf[i], mat, i);
                sets[i] = (mat->uv_set[i] == 1) ? 1 : 0;
            } else {
                static const float identity[9] = { 1,0,0, 0,1,0, 0,0,1 };
                memcpy(xf[i], identity, sizeof(identity));
                sets[i] = 0;
            }
        }
        if (loc->uvxform >= 0)
            glUniformMatrix3fv(loc->uvxform, SDGR_TEX_SLOTS, GL_FALSE, &xf[0][0]);
        if (loc->uvset >= 0)
            glUniform1iv(loc->uvset, SDGR_TEX_SLOTS, sets);
    }

    /* ---- alpha mode / culling ---- */
    if (alpha_mode == SDGR_ALPHA_BLEND) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
    } else {
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
    }
    if (double_sided) glDisable(GL_CULL_FACE);

    glBindVertexArray(mesh->vao);
    if (mesh->index_count > 0 && mesh->ebo) {
        glDrawElements(GL_TRIANGLES, mesh->index_count,
                       mesh->index_type == 3 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
                       (void *)0);
    } else {
        glDrawArrays(GL_TRIANGLES, 0, mesh->vertex_count);
    }

#ifndef NDEBUG
    {
        GLenum e = glGetError();
        if (e != GL_NO_ERROR) {
            fprintf(stderr, "renderer: GL error 0x%x while drawing\n",
                    (unsigned)e);
        }
    }
#endif
    glBindVertexArray(0);

    if (double_sided) glEnable(GL_CULL_FACE);
    if (alpha_mode == SDGR_ALPHA_BLEND) {
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
    }
}

static void r_draw_mesh(const sdge_mesh *mesh, const sdg_mat4 model,
                        const float *joints, int joint_count,
                        const sdgr_material *mat, const sdgr_texture_set *tex) {
    r_draw_mesh_program(g_r.prog, &g_r.loc, mesh, model, joints, joint_count,
                        mat, tex, NULL);
}

SDGE_API void sdgr_draw_mesh_shader(const sdge_mesh *mesh,
                                    const sdg_mat4 model,
                                    const float *joints, int joint_count,
                                    const sdgr_material *mat,
                                    const sdgr_texture_set *tex,
                                    sdgr_shader *shader) {
    if (!shader || !shader->valid) {
        r_draw_mesh(mesh, model, joints, joint_count, mat, tex);
        return;
    }
    r_draw_mesh_program(shader->prog, &shader->loc, mesh, model, joints,
                        joint_count, mat, tex, shader);
}


SDGE_API void sdgr_draw_mesh(const sdge_mesh *mesh, const sdg_mat4 model,
                             const float *joints, int joint_count,
                             const float base_color[4],
                             float metallic, float roughness, int double_sided,
                             unsigned base_texture) {
    sdgr_material m;
    memset(&m, 0, sizeof(m));
    if (base_color) memcpy(m.base_color, base_color, sizeof(m.base_color));
    m.metallic = metallic;
    m.roughness = roughness;
    m.alpha_mode = SDGR_ALPHA_OPAQUE;
    m.double_sided = double_sided;
    m.normal_scale = 1.0f;
    m.occlusion_strength = 1.0f;
    m.alpha_cutoff = 0.5f;
    for (int i = 0; i < SDGR_TEX_SLOTS; ++i) {
        m.uv_set[i] = 0;
        m.uv_scale[i][0] = 1.0f;
        m.uv_scale[i][1] = 1.0f;
    }
    sdgr_texture_set t;
    memset(&t, 0, sizeof(t));
    t.base_color = base_texture;
    r_draw_mesh(mesh, model, joints, joint_count, &m, &t);
}

SDGE_API void sdgr_draw_mesh_ex(const sdge_mesh *mesh, const sdg_mat4 model,
                                const float *joints, int joint_count,
                                const sdgr_material *mat,
                                const sdgr_texture_set *tex) {
    r_draw_mesh(mesh, model, joints, joint_count, mat, tex);
}

/* -------------------------------------------------------- custom shaders */

#define R_SHADER_FIRST_TEXTURE_UNIT 5   /* after the five material slots */

static int r_set_error(char *error, int error_size, const char *fmt, ...) {
    if (error && error_size > 0) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(error, (size_t)error_size, fmt, ap);
        va_end(ap);
    }
    return 0;
}

static int r_shader_uniform_index(const sdgr_shader *s, const char *name) {
    if (!s || !name) return -1;
    for (int i = 0; i < s->uniform_count; ++i)
        if (strcmp(s->uniforms[i].name, name) == 0) return i;
    return -1;
}

SDGE_API sdgr_shader *sdgr_shader_compile(const char *source,
                                          char *error, int error_size) {
    if (error && error_size > 0) error[0] = '\0';
    if (!g_r_inited) {
        r_set_error(error, error_size,
                    "renderer is not initialized (call sdgr_init first)");
        return NULL;
    }
    if (!source) {
        r_set_error(error, error_size, "empty shader source");
        return NULL;
    }

    sdsl_program sp;
    if (sdsl_compile(source, &sp, error, error_size) != 0) return NULL;

    char log[2048];
    unsigned vs = r_compile_log(GL_VERTEX_SHADER, sp.vertex, log, sizeof(log));
    if (!vs) {
        r_set_error(error, error_size, "vertex GLSL error: %s", log);
        sdsl_program_free(&sp);
        return NULL;
    }
    unsigned fs = r_compile_log(GL_FRAGMENT_SHADER, sp.fragment,
                                log, sizeof(log));
    if (!fs) {
        r_set_error(error, error_size, "fragment GLSL error: %s", log);
        glDeleteShader(vs);
        sdsl_program_free(&sp);
        return NULL;
    }
    unsigned prog = r_link_log(vs, fs, log, sizeof(log));
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!prog) {
        r_set_error(error, error_size, "GLSL link error: %s", log);
        sdsl_program_free(&sp);
        return NULL;
    }

    sdgr_shader *s = (sdgr_shader *)calloc(1, sizeof(*s));
    if (!s) {
        glDeleteProgram(prog);
        sdsl_program_free(&sp);
        r_set_error(error, error_size, "out of memory");
        return NULL;
    }
    s->prog = prog;
    s->valid = 1;
    r_locs_get(prog, &s->loc);

    s->uniform_count = sp.uniform_count;
    s->sampler_count = 0;
    for (int i = 0; i < sp.uniform_count; ++i) {
        s->uniforms[i] = sp.uniforms[i];
        s->uniform_loc[i] = glGetUniformLocation(prog, sp.uniforms[i].name);
        s->uniform_tex[i] = 0;
        s->uniform_unit[i] = 0;
        if (sp.uniforms[i].type == SDSL_UNIFORM_SAMPLER2D) {
            int unit = R_SHADER_FIRST_TEXTURE_UNIT + s->sampler_count++;
            s->uniform_unit[i] = unit;
            if (s->uniform_loc[i] >= 0) {
                glUseProgram(prog);
                glUniform1i(s->uniform_loc[i], unit);
            }
        }
    }
    glUseProgram(g_r.prog);

    s->vs_src = sp.vertex;
    s->fs_src = sp.fragment;
    sp.vertex = NULL;
    sp.fragment = NULL;
    sdsl_program_free(&sp);
    return s;
}

SDGE_API sdgr_shader *sdgr_shader_load_file(const char *path,
                                            char *error, int error_size) {
    if (error && error_size > 0) error[0] = '\0';
    if (!path) {
        r_set_error(error, error_size, "no shader path given");
        return NULL;
    }
    unsigned char *data = NULL;
    size_t size = 0;
    if (sdga_read_file(path, &data, &size) != 0) {
        r_set_error(error, error_size, "cannot read shader file '%s'", path);
        return NULL;
    }
    char *text = (char *)malloc(size + 1);
    if (!text) {
        free(data);
        r_set_error(error, error_size, "out of memory");
        return NULL;
    }
    if (size > 0) memcpy(text, data, size);
    text[size] = '\0';
    free(data);

    sdgr_shader *s = sdgr_shader_compile(text, error, error_size);
    free(text);
    return s;
}

SDGE_API void sdgr_shader_destroy(sdgr_shader *shader) {
    if (!shader) return;
    if (shader->prog) glDeleteProgram(shader->prog);
    free(shader->vs_src);
    free(shader->fs_src);
    free(shader);
}

SDGE_API int sdgr_shader_valid(const sdgr_shader *shader) {
    return shader && shader->valid ? 1 : 0;
}

SDGE_API const char *sdgr_shader_vertex_glsl(const sdgr_shader *shader) {
    return shader ? shader->vs_src : NULL;
}

SDGE_API const char *sdgr_shader_fragment_glsl(const sdgr_shader *shader) {
    return shader ? shader->fs_src : NULL;
}

SDGE_API int sdgr_shader_set_float(sdgr_shader *s, const char *name,
                                   float value) {
    if (!s || !s->valid) return 0;
    int i = r_shader_uniform_index(s, name);
    if (i < 0 || s->uniforms[i].type != SDSL_UNIFORM_FLOAT) return 0;
    if (s->uniform_loc[i] < 0) return 0;
    glUseProgram(s->prog);
    glUniform1f(s->uniform_loc[i], value);
    glUseProgram(g_r.prog);
    return 1;
}

SDGE_API int sdgr_shader_set_int(sdgr_shader *s, const char *name, int value) {
    if (!s || !s->valid) return 0;
    int i = r_shader_uniform_index(s, name);
    if (i < 0) return 0;
    if (s->uniforms[i].type != SDSL_UNIFORM_INT &&
        s->uniforms[i].type != SDSL_UNIFORM_BOOL) return 0;
    if (s->uniform_loc[i] < 0) return 0;
    glUseProgram(s->prog);
    glUniform1i(s->uniform_loc[i], value);
    glUseProgram(g_r.prog);
    return 1;
}

SDGE_API int sdgr_shader_set_vec2(sdgr_shader *s, const char *name,
                                  float x, float y) {
    if (!s || !s->valid) return 0;
    int i = r_shader_uniform_index(s, name);
    if (i < 0 || s->uniforms[i].type != SDSL_UNIFORM_VEC2) return 0;
    if (s->uniform_loc[i] < 0) return 0;
    glUseProgram(s->prog);
    glUniform2f(s->uniform_loc[i], x, y);
    glUseProgram(g_r.prog);
    return 1;
}

SDGE_API int sdgr_shader_set_vec3(sdgr_shader *s, const char *name,
                                  float x, float y, float z) {
    if (!s || !s->valid) return 0;
    int i = r_shader_uniform_index(s, name);
    if (i < 0 || s->uniforms[i].type != SDSL_UNIFORM_VEC3) return 0;
    if (s->uniform_loc[i] < 0) return 0;
    glUseProgram(s->prog);
    glUniform3f(s->uniform_loc[i], x, y, z);
    glUseProgram(g_r.prog);
    return 1;
}

SDGE_API int sdgr_shader_set_vec4(sdgr_shader *s, const char *name,
                                  float x, float y, float z, float w) {
    if (!s || !s->valid) return 0;
    int i = r_shader_uniform_index(s, name);
    if (i < 0 || s->uniforms[i].type != SDSL_UNIFORM_VEC4) return 0;
    if (s->uniform_loc[i] < 0) return 0;
    glUseProgram(s->prog);
    glUniform4f(s->uniform_loc[i], x, y, z, w);
    glUseProgram(g_r.prog);
    return 1;
}

SDGE_API int sdgr_shader_set_texture(sdgr_shader *s, const char *name,
                                     unsigned texture) {
    if (!s || !s->valid) return 0;
    int i = r_shader_uniform_index(s, name);
    if (i < 0 || s->uniforms[i].type != SDSL_UNIFORM_SAMPLER2D) return 0;
    if (s->uniform_loc[i] < 0) return 0;
    s->uniform_tex[i] = texture;
    glUseProgram(s->prog);
    glActiveTexture(GL_TEXTURE0 + (unsigned)s->uniform_unit[i]);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(s->uniform_loc[i], s->uniform_unit[i]);
    glActiveTexture(GL_TEXTURE0);
    glUseProgram(g_r.prog);
    return 1;
}

SDGE_API void sdgr_shader_set_time(sdgr_shader *shader, float seconds) {
    if (shader) shader->time_value = seconds;
}

/* ------------------------------------------------------------------ lines */

static void r_draw_lines(const float *vertices, int vertex_count,
                         const float color[4], int always) {
    if (!g_r_inited || !g_r.lines.prog || !vertices || vertex_count < 2)
        return;

    glUseProgram(g_r.lines.prog);
    glUniformMatrix4fv(g_r.lines.loc_proj, 1, GL_FALSE, g_r.proj);
    glUniformMatrix4fv(g_r.lines.loc_view, 1, GL_FALSE, g_r.view);
    float c[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    if (color) memcpy(c, color, sizeof(c));
    glUniform4fv(g_r.lines.loc_color, 1, c);

    glBindBuffer(GL_ARRAY_BUFFER, g_r.lines.vbo);
    size_t bytes = (size_t)vertex_count * 3u * sizeof(float);
    if ((int)g_r.lines.vbo_cap < (int)bytes) {
        glBufferData(GL_ARRAY_BUFFER, bytes, NULL, GL_DYNAMIC_DRAW);
        g_r.lines.vbo_cap = (int)bytes;
    }
    glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, vertices);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);

    if (always) {
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
    } else {
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
    }
    glDisable(GL_CULL_FACE);
    glDrawArrays(GL_LINES, 0, vertex_count);
    glEnable(GL_CULL_FACE);
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
}

SDGE_API void sdgr_draw_lines(const float *vertices, int vertex_count,
                              const float color[4]) {
    r_draw_lines(vertices, vertex_count, color, 0);
}

SDGE_API void sdgr_draw_lines_always(const float *vertices, int vertex_count,
                                     const float color[4]) {
    r_draw_lines(vertices, vertex_count, color, 1);
}

SDGE_API void sdgr_draw_box_wire(const float center[3], const float half[3],
                                 const sdg_mat4 *rotation,
                                 const float color[4]) {
    if (!center || !half) return;
    /* the 8 corners of the box */
    float corner[8][3];
    for (int i = 0; i < 8; ++i) {
        float sx = (i & 1) ? 1.0f : -1.0f;
        float sy = (i & 2) ? 1.0f : -1.0f;
        float sz = (i & 4) ? 1.0f : -1.0f;
        float p[3] = { sx * half[0], sy * half[1], sz * half[2] };
        if (rotation) {
            float w[3];
            m4_mul_vec3_dir(w, *rotation, p);
            p[0] = w[0]; p[1] = w[1]; p[2] = w[2];
        }
        corner[i][0] = center[0] + p[0];
        corner[i][1] = center[1] + p[1];
        corner[i][2] = center[2] + p[2];
    }
    /* the 12 edges */
    static const int edges[12][2] = {
        { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 },
        { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 },
        { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }
    };
    float verts[12 * 2 * 3];
    int o = 0;
    for (int e = 0; e < 12; ++e) {
        for (int k = 0; k < 2; ++k) {
            const float *p = corner[edges[e][k]];
            verts[o++] = p[0];
            verts[o++] = p[1];
            verts[o++] = p[2];
        }
    }
    r_draw_lines(verts, 24, color, 0);
}

SDGE_API int sdgr_screenshot(const char *path) {
    if (!g_r_inited || !path) return -1;
    int vp[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, vp);
    int w = vp[2], h = vp[3];
    if (w <= 0 || h <= 0) return -2;

    unsigned char *row = (unsigned char *)malloc((size_t)w * 3u);
    if (!row) return -3;

    FILE *f = fopen(path, "wb");
    if (!f) { free(row); return -4; }
    fprintf(f, "P6\n%d %d\n255\n", w, h);

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    for (int y = h - 1; y >= 0; --y) {
        glReadPixels(0, y, w, 1, GL_RGB, GL_UNSIGNED_BYTE, row);
        fwrite(row, 1, (size_t)w * 3u, f);
    }
    fclose(f);
    free(row);
    return 0;
}

#else /* SDGE_NO_GL */

SDGE_API int  sdgr_init(void) { return -1; }
SDGE_API void sdgr_shutdown(void) {}
SDGE_API void sdgr_resize(int w, int h) { (void)w; (void)h; }
SDGE_API void sdgr_begin_frame(const float c[4]) { (void)c; }
SDGE_API void sdgr_end_frame(void) {}
SDGE_API void sdgr_set_camera(const sdg_mat4 v, const sdg_mat4 p,
                               const float cp[3]) { (void)v; (void)p; (void)cp; }
SDGE_API int  sdgr_sphere_visible(const float c[3], float r) { (void)c; (void)r; return 1; }
SDGE_API void sdgr_get_camera_pos(float p[3]) { if (p) { p[0]=p[1]=p[2]=0; } }
SDGE_API void sdgr_set_ambient(float r, float g, float b) { (void)r; (void)g; (void)b; }
SDGE_API void sdgr_set_sun(const float d[3], const float c[3], float i) { (void)d; (void)c; (void)i; }
SDGE_API void sdgr_set_point_light(int i, const float p[3], const float c[3],
                                    float in, float r) { (void)i; (void)p; (void)c; (void)in; (void)r; }
SDGE_API unsigned sdgr_upload_texture(int w, int h,
                                       const unsigned char *rgba8) {
    (void)w; (void)h; (void)rgba8; return 0;
}
SDGE_API unsigned sdgr_upload_texture_ex(int w, int h,
                                          const unsigned char *rgba8,
                                          const sdgr_texture_desc *desc) {
    (void)w; (void)h; (void)rgba8; (void)desc; return 0;
}
SDGE_API void sdgr_free_texture(unsigned tex) { (void)tex; }
SDGE_API void sdgr_draw_mesh(const sdge_mesh *m, const sdg_mat4 mo,
                             const float *j, int jc, const float bc[4],
                             float me, float ro, int ds, unsigned bt) {
    (void)m; (void)mo; (void)j; (void)jc; (void)bc; (void)me; (void)ro;
    (void)ds; (void)bt;
}
SDGE_API void sdgr_draw_mesh_ex(const sdge_mesh *m, const sdg_mat4 mo,
                                const float *j, int jc, const sdgr_material *mat,
                                const sdgr_texture_set *tex) {
    (void)m; (void)mo; (void)j; (void)jc; (void)mat; (void)tex;
}
SDGE_API sdgr_shader *sdgr_shader_compile(const char *src,
                                          char *error, int error_size) {
    (void)src;
    if (error && error_size > 0)
        snprintf(error, (size_t)error_size,
                 "this build has no OpenGL (SDGE_NO_GL)");
    return NULL;
}
SDGE_API sdgr_shader *sdgr_shader_load_file(const char *path,
                                            char *error, int error_size) {
    (void)path;
    if (error && error_size > 0)
        snprintf(error, (size_t)error_size,
                 "this build has no OpenGL (SDGE_NO_GL)");
    return NULL;
}
SDGE_API void sdgr_shader_destroy(sdgr_shader *s) { (void)s; }
SDGE_API int  sdgr_shader_valid(const sdgr_shader *s) { (void)s; return 0; }
SDGE_API const char *sdgr_shader_vertex_glsl(const sdgr_shader *s) {
    (void)s; return NULL;
}
SDGE_API const char *sdgr_shader_fragment_glsl(const sdgr_shader *s) {
    (void)s; return NULL;
}
SDGE_API int sdgr_shader_set_float(sdgr_shader *s, const char *n, float v) {
    (void)s; (void)n; (void)v; return 0;
}
SDGE_API int sdgr_shader_set_int(sdgr_shader *s, const char *n, int v) {
    (void)s; (void)n; (void)v; return 0;
}
SDGE_API int sdgr_shader_set_vec2(sdgr_shader *s, const char *n,
                                  float x, float y) {
    (void)s; (void)n; (void)x; (void)y; return 0;
}
SDGE_API int sdgr_shader_set_vec3(sdgr_shader *s, const char *n,
                                  float x, float y, float z) {
    (void)s; (void)n; (void)x; (void)y; (void)z; return 0;
}
SDGE_API int sdgr_shader_set_vec4(sdgr_shader *s, const char *n,
                                  float x, float y, float z, float w) {
    (void)s; (void)n; (void)x; (void)y; (void)z; (void)w; return 0;
}
SDGE_API int sdgr_shader_set_texture(sdgr_shader *s, const char *n,
                                     unsigned tex) {
    (void)s; (void)n; (void)tex; return 0;
}
SDGE_API void sdgr_shader_set_time(sdgr_shader *s, float t) { (void)s; (void)t; }
SDGE_API void sdgr_draw_mesh_shader(const sdge_mesh *m, const sdg_mat4 mo,
                                    const float *j, int jc,
                                    const sdgr_material *mat,
                                    const sdgr_texture_set *tex,
                                    sdgr_shader *s) {
    (void)m; (void)mo; (void)j; (void)jc; (void)mat; (void)tex; (void)s;
}
SDGE_API void sdgr_draw_lines(const float *v, int vc, const float c[4]) {
    (void)v; (void)vc; (void)c;
}
SDGE_API void sdgr_draw_lines_always(const float *v, int vc, const float c[4]) {
    (void)v; (void)vc; (void)c;
}
SDGE_API void sdgr_draw_box_wire(const float c[3], const float h[3],
                                 const sdg_mat4 *r, const float col[4]) {
    (void)c; (void)h; (void)r; (void)col;
}
SDGE_API int sdgr_screenshot(const char *p) { (void)p; return -1; }

#endif /* SDGE_NO_GL */
