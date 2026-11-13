/* renderer.c - Silver Diamond Game Engine :: forward renderer implementation
 *
 * glad/GL are a PRIVATE dependency of this TU: the engine DLL loads GL
 * itself, applications only see the sdgr_* API.
 */
#include "renderer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

typedef struct {
    unsigned prog;
    int loc_proj, loc_view, loc_model, loc_joints, loc_skinned;
    int loc_cam, loc_ambient, loc_sundir, loc_suncol, loc_suni;
    int loc_ppos, loc_pcol, loc_pcount;
    int loc_base, loc_metal, loc_rough, loc_dbl;
    int loc_emissive, loc_normal_scale, loc_occl_strength, loc_alpha_cutoff;
    int loc_alpha_mode, loc_has_tangent;
    int loc_tex[5];              /* uBaseTex, uMRTex, ... */
    int loc_has_tex[5];         /* uHasBase, uHasMR, ... */
    int loc_uvxform, loc_uvset;

    float view[16], proj[16], cam_pos[3];
    float frustum[6][4];
    int   point_count;
} r_state;

static r_state g_r;
static int g_r_inited;

static unsigned r_compile(unsigned type, const char *src) {
    unsigned s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    int ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        GLsizei n = 0;
        glGetShaderInfoLog(s, sizeof(log), &n, log);
        fprintf(stderr, "renderer: shader compile failed:\n%.*s\n", (int)n, log);
        glDeleteShader(s);
        return 0;
    }
    return s;
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

    unsigned prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    int ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        GLsizei n = 0;
        glGetProgramInfoLog(prog, sizeof(log), &n, log);
        fprintf(stderr, "renderer: program link failed:\n%.*s\n", (int)n, log);
        glDeleteProgram(prog);
        glDeleteShader(vs);
        glDeleteShader(fs);
        return -1;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);

    memset(&g_r, 0, sizeof(g_r));
    g_r.prog = prog;
    g_r.loc_proj    = glGetUniformLocation(prog, "uProj");
    g_r.loc_view    = glGetUniformLocation(prog, "uView");
    g_r.loc_model   = glGetUniformLocation(prog, "uModel");
    g_r.loc_joints  = glGetUniformLocation(prog, "uJoints");
    g_r.loc_skinned = glGetUniformLocation(prog, "uSkinned");
    g_r.loc_cam     = glGetUniformLocation(prog, "uCamPos");
    g_r.loc_ambient = glGetUniformLocation(prog, "uAmbient");
    g_r.loc_sundir  = glGetUniformLocation(prog, "uSunDir");
    g_r.loc_suncol  = glGetUniformLocation(prog, "uSunColor");
    g_r.loc_suni    = glGetUniformLocation(prog, "uSunI");
    g_r.loc_ppos    = glGetUniformLocation(prog, "uPointPos[0]");
    g_r.loc_pcol    = glGetUniformLocation(prog, "uPointCol[0]");
    g_r.loc_pcount  = glGetUniformLocation(prog, "uPointCount");
    g_r.loc_base    = glGetUniformLocation(prog, "uBaseColor");
    g_r.loc_metal   = glGetUniformLocation(prog, "uMetallic");
    g_r.loc_rough   = glGetUniformLocation(prog, "uRoughness");
    g_r.loc_dbl     = glGetUniformLocation(prog, "uDoubleSided");
    g_r.loc_emissive= glGetUniformLocation(prog, "uEmissive");
    g_r.loc_normal_scale = glGetUniformLocation(prog, "uNormalScale");
    g_r.loc_occl_strength = glGetUniformLocation(prog, "uOcclusionStrength");
    g_r.loc_alpha_cutoff = glGetUniformLocation(prog, "uAlphaCutoff");
    g_r.loc_alpha_mode = glGetUniformLocation(prog, "uAlphaMode");
    g_r.loc_has_tangent = glGetUniformLocation(prog, "uHasTangent");
    {
        static const char *tex_names[5] = {
            "uBaseTex", "uMRTex", "uNormalTex", "uOcclusionTex", "uEmissiveTex"
        };
        static const char *has_names[5] = {
            "uHasBase", "uHasMR", "uHasNormal", "uHasOcclusion", "uHasEmissive"
        };
        for (int i = 0; i < 5; ++i) {
            g_r.loc_tex[i] = glGetUniformLocation(prog, tex_names[i]);
            g_r.loc_has_tex[i] = glGetUniformLocation(prog, has_names[i]);
        }
    }
    g_r.loc_uvxform = glGetUniformLocation(prog, "uUVXform[0]");
    g_r.loc_uvset  = glGetUniformLocation(prog, "uUVSet[0]");

    m4_identity(g_r.view);
    m4_identity(g_r.proj);
    g_r.point_count = 0;
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
    glDeleteProgram(g_r.prog);
    memset(&g_r, 0, sizeof(g_r));
    g_r_inited = 0;
}

SDGE_API void sdgr_resize(int width, int height) {
    if (!g_r_inited) return;
    glViewport(0, 0, width > 0 ? width : 1, height > 0 ? height : 1);
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
    glUniform1i(g_r.loc_pcount, 0);
    for (int i = 0; i < SDGR_TEX_SLOTS; ++i)
        if (g_r.loc_has_tex[i] >= 0) glUniform1i(g_r.loc_has_tex[i], 0);
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
    glUseProgram(g_r.prog);
    glUniform3f(g_r.loc_ambient, r, g, b);
}

SDGE_API void sdgr_set_sun(const float dir[3], const float color[3],
                           float intensity) {
    if (!g_r_inited) return;
    float d[3] = { 0, -1, 0 }, c[3] = { 1, 1, 1 };
    if (dir)   memcpy(d, dir, sizeof(float) * 3);
    if (color) memcpy(c, color, sizeof(float) * 3);
    float len = sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
    if (len > 1e-9f) { d[0] /= len; d[1] /= len; d[2] /= len; }
    glUseProgram(g_r.prog);
    glUniform3fv(g_r.loc_sundir, 1, d);
    glUniform3fv(g_r.loc_suncol, 1, c);
    glUniform1f(g_r.loc_suni, intensity);
}

SDGE_API void sdgr_set_point_light(int index, const float pos[3],
                                   const float color[3],
                                   float intensity, float radius) {
    if (!g_r_inited) return;
    if (index < 0 || index >= SDGR_MAX_POINT_LIGHTS) return;
    float p[4], c[4];
    p[0] = pos ? pos[0] : 0; p[1] = pos ? pos[1] : 0;
    p[2] = pos ? pos[2] : 0; p[3] = radius > 0.0f ? radius : 1.0f;
    c[0] = color ? color[0] : 1; c[1] = color ? color[1] : 1;
    c[2] = color ? color[2] : 1; c[3] = intensity;
    glUseProgram(g_r.prog);
    glUniform4fv(g_r.loc_ppos + index, 1, p);
    glUniform4fv(g_r.loc_pcol + index, 1, c);
    if (index + 1 > g_r.point_count) {
        g_r.point_count = index + 1;
        glUniform1i(g_r.loc_pcount, g_r.point_count);
    }
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

static void r_draw_mesh(const sdge_mesh *mesh, const sdg_mat4 model,
                        const float *joints, int joint_count,
                        const sdgr_material *mat, const sdgr_texture_set *tex) {
    if (!g_r_inited || !mesh || !mesh->vao) return;

    int skinned = (mesh->has_skin && joints && joint_count > 0);
    if (skinned && joint_count > SDGR_MAX_JOINTS) joint_count = SDGR_MAX_JOINTS;

    glUseProgram(g_r.prog);
    glUniformMatrix4fv(g_r.loc_proj, 1, GL_FALSE, g_r.proj);
    glUniformMatrix4fv(g_r.loc_view, 1, GL_FALSE, g_r.view);
    glUniformMatrix4fv(g_r.loc_model, 1, GL_FALSE,
                       model ? model : r_identity());
    glUniform1i(g_r.loc_skinned, skinned ? 1 : 0);
    if (skinned)
        glUniformMatrix4fv(g_r.loc_joints, joint_count, GL_FALSE, joints);
    glUniform3fv(g_r.loc_cam, 1, g_r.cam_pos);

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
    glUniform4fv(g_r.loc_base, 1, bc);
    glUniform3fv(g_r.loc_emissive, 1, em);
    glUniform1f(g_r.loc_metal, metallic);
    glUniform1f(g_r.loc_rough, roughness < 0.045f ? 0.045f : roughness);
    glUniform1f(g_r.loc_normal_scale, normal_scale);
    glUniform1f(g_r.loc_occl_strength, occl);
    glUniform1f(g_r.loc_alpha_cutoff, cutoff);
    glUniform1i(g_r.loc_alpha_mode, alpha_mode);
    glUniform1i(g_r.loc_dbl, double_sided ? 1 : 0);
    glUniform1i(g_r.loc_has_tangent, 0);

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
        int has = (texid[i] != 0 && mesh->has_texcoords); if (i == SDGR_SLOT_NORMAL) has = 0;
        if (g_r.loc_has_tex[i] >= 0) glUniform1i(g_r.loc_has_tex[i], has);
        if (g_r.loc_tex[i] >= 0) {
            glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_2D, has ? texid[i] : 0);
            glUniform1i(g_r.loc_tex[i], i);
        }
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
        if (g_r.loc_uvxform >= 0)
            glUniformMatrix3fv(g_r.loc_uvxform, SDGR_TEX_SLOTS, GL_FALSE, &xf[0][0]);
        if (g_r.loc_uvset >= 0)
            glUniform1iv(g_r.loc_uvset, SDGR_TEX_SLOTS, sets);
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
SDGE_API int sdgr_screenshot(const char *p) { (void)p; return -1; }

#endif /* SDGE_NO_GL */
