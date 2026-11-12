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
"uniform mat4 uProj, uView, uModel;\n"
"uniform mat4 uJoints[" SDGR_MAX_JOINTS_STR "];\n"
"uniform int uSkinned;\n"
"out vec3 vN;\n"
"out vec3 vWpos;\n"
"out vec2 vUV;\n"
"void main() {\n"
"    mat4 skin = mat4(1.0);\n"
"    if (uSkinned == 1) {\n"
"        skin = uJoints[int(aJoint.x)] * aWeight.x\n"
"             + uJoints[int(aJoint.y)] * aWeight.y\n"
"             + uJoints[int(aJoint.z)] * aWeight.z\n"
"             + uJoints[int(aJoint.w)] * aWeight.w;\n"
"    }\n"
"    vec4 world = uModel * skin * vec4(aPos, 1.0);\n"
"    vWpos = world.xyz;\n"
"    vN = mat3(uModel) * mat3(skin) * aNrm;\n"
"    vUV = aUV;\n"
"    gl_Position = uProj * uView * world;\n"
"}\n";

static const char *R_FS =
"#version 330 core\n"
"in vec3 vN;\n"
"in vec3 vWpos;\n"
"in vec2 vUV;\n"
"uniform sampler2D uBaseTex;\n"
"uniform int uHasTex;\n"
"uniform vec3 uCamPos;\n"
"uniform vec3 uAmbient;\n"
"uniform vec3 uSunDir, uSunColor;\n"
"uniform float uSunI;\n"
"uniform vec4 uPointPos[" "8" "];\n"
"uniform vec4 uPointCol[" "8" "];\n"
"uniform int uPointCount;\n"
"uniform vec4 uBaseColor;\n"
"uniform float uMetallic, uRoughness;\n"
"uniform int uDoubleSided;\n"
"out vec4 frag;\n"
"float ggx(float ndh, float a) {\n"
"    float a2 = a * a;\n"
"    float d = ndh * ndh * (a2 - 1.0) + 1.0;\n"
"    return a2 / (3.14159265 * d * d);\n"
"}\n"
"void main() {\n"
"    vec4 base = uBaseColor;\n"
"    if (uHasTex == 1) base *= texture(uBaseTex, vUV);\n"
"    if (base.a <= 0.002) discard;\n"
"    vec3 N = normalize(vN);\n"
"    if (uDoubleSided == 1 && !gl_FrontFacing) N = -N;\n"
"    vec3 V = normalize(uCamPos - vWpos);\n"
"    vec3 diffuse = base.rgb * (1.0 - uMetallic);\n"
"    vec3 f0 = mix(vec3(0.04), base.rgb, uMetallic);\n"
"    float a = uRoughness * uRoughness + 1e-3;\n"
"    vec3 col = uAmbient * base.rgb;\n"
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
"    frag = vec4(col, base.a);\n"
"}\n";

typedef struct {
    unsigned prog;
    int loc_proj, loc_view, loc_model, loc_joints, loc_skinned;
    int loc_cam, loc_ambient, loc_sundir, loc_suncol, loc_suni;
    int loc_ppos, loc_pcol, loc_pcount;
    int loc_base, loc_metal, loc_rough, loc_dbl;
    int loc_base_tex, loc_has_tex;

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
    g_r.loc_base_tex= glGetUniformLocation(prog, "uBaseTex");
    g_r.loc_has_tex = glGetUniformLocation(prog, "uHasTex");

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
    if (g_r.loc_has_tex >= 0) glUniform1i(g_r.loc_has_tex, 0);
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
    if (!g_r_inited || !rgba8 || w <= 0 || h <= 0) return 0;
    unsigned tex = 0;
    glGenTextures(1, &tex);
    if (!tex) return 0;
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, rgba8);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glGenerateMipmap(GL_TEXTURE_2D);
    return tex;
}

SDGE_API void sdgr_free_texture(unsigned tex) {
    if (tex) glDeleteTextures(1, &tex);
}

SDGE_API void sdgr_draw_mesh(const sdge_mesh *mesh, const sdg_mat4 model,
                             const float *joints, int joint_count,
                             const float base_color[4],
                             float metallic, float roughness, int double_sided,
                             unsigned base_texture) {
    if (!g_r_inited || !mesh || !mesh->vao) return;

    int skinned = (mesh->has_skin && joints && joint_count > 0);
    if (skinned && joint_count > SDGR_MAX_JOINTS) joint_count = SDGR_MAX_JOINTS;
    int use_tex = (base_texture != 0 && mesh->has_texcoords);

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
    if (base_color) memcpy(bc, base_color, sizeof(bc));
    glUniform4fv(g_r.loc_base, 1, bc);
    glUniform1f(g_r.loc_metal, metallic);
    glUniform1f(g_r.loc_rough, roughness < 0.045f ? 0.045f : roughness);
    glUniform1i(g_r.loc_dbl, (double_sided || mesh->double_sided) ? 1 : 0);

    if (g_r.loc_has_tex >= 0) glUniform1i(g_r.loc_has_tex, use_tex ? 1 : 0);
    if (g_r.loc_base_tex >= 0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, use_tex ? base_texture : 0);
        glUniform1i(g_r.loc_base_tex, 0);
    }

    if (double_sided || mesh->double_sided) glDisable(GL_CULL_FACE);

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

    if (double_sided || mesh->double_sided) glEnable(GL_CULL_FACE);
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
SDGE_API void sdgr_set_ambient(float r, float g, float b) { (void)r; (void)g; (void)b; }
SDGE_API void sdgr_set_sun(const float d[3], const float c[3], float i) { (void)d; (void)c; (void)i; }
SDGE_API void sdgr_set_point_light(int i, const float p[3], const float c[3],
                                    float in, float r) { (void)i; (void)p; (void)c; (void)in; (void)r; }
SDGE_API unsigned sdgr_upload_texture(int w, int h,
                                       const unsigned char *rgba8) {
    (void)w; (void)h; (void)rgba8; return 0;
}
SDGE_API void sdgr_free_texture(unsigned tex) { (void)tex; }
SDGE_API void sdgr_draw_mesh(const sdge_mesh *m, const sdg_mat4 mo,
                             const float *j, int jc, const float bc[4],
                             float me, float ro, int ds, unsigned bt) {
    (void)m; (void)mo; (void)j; (void)jc; (void)bc; (void)me; (void)ro;
    (void)ds; (void)bt;
}
SDGE_API int sdgr_screenshot(const char *p) { (void)p; return -1; }

#endif /* SDGE_NO_GL */