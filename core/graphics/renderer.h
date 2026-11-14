/* renderer.h - Silver Diamond Game Engine :: forward renderer (OpenGL 3.3+)
 *
 * One shader for everything skinned/unskinned, PBR-lite lighting:
 * ambient + one directional "sun" + up to 8 point lights.
 *
 * Texturing (glTF PBR):
 *   - up to five texture slots per material: base color, metallic-roughness,
 *     normal, occlusion, emissive;
 *   - base color and emissive are uploaded as sRGB (GL_SRGB8_ALPHA8), the
 *     data maps stay linear - exactly like the glTF specification asks;
 *   - normal maps use the TANGENT attribute when the mesh has one and fall
 *     back to a screen-space cotangent frame otherwise;
 *   - alphaMode OPAQUE / MASK (alpha test) / BLEND (alpha blending);
 *   - every slot can use TEXCOORD_0 or TEXCOORD_1 and carries its own
 *     KHR_texture_transform (offset / rotation / scale).
 *
 * The renderer keeps ALL GL state internal (glad is a private dependency,
 * loaded once inside sdgr_init() - applications never touch GL headers).
 *
 * Typical frame:
 *     sdgr_resize(w, h);
 *     sdgr_begin_frame(clear);
 *     sdgr_set_camera(view, proj, cam_pos);
 *     sdgr_set_ambient(0.25f, 0.25f, 0.3f);
 *     sdgr_set_sun(dir, color, 2.0f);
 *     sdgr_draw_mesh(&mesh, model, joints, joint_count, base, metal, rough,
 *                    0, base_tex);   // base_tex = 0 means no texture
 *     sdgr_end_frame();
 */
#ifndef SDGE_RENDERER_H
#define SDGE_RENDERER_H

#include "../sdge_def.h"
#include "../math/math3d.h"
#include "mesh.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SDGR_MAX_POINT_LIGHTS 8
#define SDGR_MAX_JOINTS       128

/* material texture slots (also the order of the shader's sampler units) */
#define SDGR_TEX_SLOTS 5
#define SDGR_SLOT_BASE      0   /* base color (sRGB)          */
#define SDGR_SLOT_MR        1   /* metallic-roughness (linear) */
#define SDGR_SLOT_NORMAL    2   /* tangent-space normal (linear) */
#define SDGR_SLOT_OCCLUSION 3   /* ambient occlusion (linear)  */
#define SDGR_SLOT_EMISSIVE  4   /* emissive (sRGB)             */

/* alpha handling (mirrors the glTF alphaMode values) */
#define SDGR_ALPHA_OPAQUE 0
#define SDGR_ALPHA_MASK   1
#define SDGR_ALPHA_BLEND  2

/* GL texture parameters (the numeric values of the OpenGL enums - the glTF
 * sampler values are identical, so they can be passed straight through) */
#define SDGR_NEAREST                0x2600
#define SDGR_LINEAR                 0x2601
#define SDGR_NEAREST_MIPMAP_NEAREST 0x2700
#define SDGR_LINEAR_MIPMAP_NEAREST  0x2701
#define SDGR_NEAREST_MIPMAP_LINEAR  0x2702
#define SDGR_LINEAR_MIPMAP_LINEAR   0x2703
#define SDGR_REPEAT                 0x2901
#define SDGR_CLAMP_TO_EDGE          0x812F
#define SDGR_MIRRORED_REPEAT        0x8370

/* GL texture names of one material (0 = slot unused) */
typedef struct {
    unsigned base_color;
    unsigned metallic_roughness;
    unsigned normal;
    unsigned occlusion;
    unsigned emissive;
} sdgr_texture_set;

/* CPU side material description */
typedef struct {
    float base_color[4];
    float metallic;
    float roughness;
    float emissive[3];
    float normal_scale;         /* multiplier of the normal map XY */
    float occlusion_strength;   /* 0 = ignore the occlusion map */
    float alpha_cutoff;         /* used by SDGR_ALPHA_MASK */
    int   alpha_mode;           /* SDGR_ALPHA_* */
    int   double_sided;

    /* per slot: UV set (0 = TEXCOORD_0, 1 = TEXCOORD_1) and texture
     * transform (offset / rotation in radians / scale) */
    int   uv_set[SDGR_TEX_SLOTS];
    float uv_offset[SDGR_TEX_SLOTS][2];
    float uv_rot[SDGR_TEX_SLOTS];
    float uv_scale[SDGR_TEX_SLOTS][2];
} sdgr_material;

/* how a texture is uploaded to the GPU */
typedef struct {
    int srgb;                   /* 1 = GL_SRGB8_ALPHA8 (color textures) */
    int wrap_s, wrap_t;         /* GL_REPEAT / GL_CLAMP_TO_EDGE / ... */
    int min_filter;             /* GL_LINEAR, GL_LINEAR_MIPMAP_LINEAR, ... */
    int mag_filter;             /* GL_LINEAR / GL_NEAREST */
    int mipmaps;                /* 1 = generate a mip chain */
} sdgr_texture_desc;

SDGE_API int  sdgr_init(void);
SDGE_API void sdgr_shutdown(void);

SDGE_API void sdgr_resize(int width, int height);
SDGE_API void sdgr_begin_frame(const float clear_color[4]);
SDGE_API void sdgr_end_frame(void);

SDGE_API void sdgr_set_camera(const sdg_mat4 view, const sdg_mat4 proj,
                              const float cam_pos[3]);
SDGE_API int  sdgr_sphere_visible(const float center[3], float radius);
SDGE_API void sdgr_get_camera_pos(float out_pos[3]);

SDGE_API void sdgr_set_ambient(float r, float g, float b);
SDGE_API void sdgr_set_sun(const float dir[3], const float color[3],
                           float intensity);
SDGE_API void sdgr_set_point_light(int index, const float pos[3],
                                   const float color[3],
                                   float intensity, float radius);

/* texture upload/free (RGBA8) - the caller owns the result */
SDGE_API unsigned sdgr_upload_texture(int w, int h,
                                      const unsigned char *rgba8);
/* same, but with an explicit format/sampler description */
SDGE_API unsigned sdgr_upload_texture_ex(int w, int h,
                                          const unsigned char *rgba8,
                                          const sdgr_texture_desc *desc);
SDGE_API void     sdgr_free_texture(unsigned tex);

/* `base_texture` = 0 means "use base_color only". The texture is sampled
 * with the mesh's TEXCOORD_0; if the mesh has none, the texture is ignored. */
SDGE_API void sdgr_draw_mesh(const sdge_mesh *mesh, const sdg_mat4 model,
                             const float *joints, int joint_count,
                             const float base_color[4],
                             float metallic, float roughness, int double_sided,
                             unsigned base_texture);

/* full material draw (textures, normal map, alpha mode, ...) */
SDGE_API void sdgr_draw_mesh_ex(const sdge_mesh *mesh, const sdg_mat4 model,
                                const float *joints, int joint_count,
                                const sdgr_material *mat,
                                const sdgr_texture_set *tex);

/* ------------------------------------------------------------------ lines
 * A simple line batch for the debug helpers and the scene editor gizmos.
 * `vertices` is a list of xyz triples, every two of them form one line:
 *     float seg[] = { x0,y0,z0, x1,y1,z1, ... };
 *     sdgr_draw_lines(seg, 6, color);
 * The lines are drawn with the depth test on, so they can be hidden by the
 * model; use sdgr_draw_lines_always() for the helpers that must be seen
 * through the geometry. */
SDGE_API void sdgr_draw_lines(const float *vertices, int vertex_count,
                              const float color[4]);
SDGE_API void sdgr_draw_lines_always(const float *vertices, int vertex_count,
                                     const float color[4]);
/* the 12 edges of a box (center + half extents, optionally rotated) */
SDGE_API void sdgr_draw_box_wire(const float center[3], const float half[3],
                                 const sdg_mat4 *rotation,
                                 const float color[4]);

SDGE_API int  sdgr_screenshot(const char *path);

#ifdef __cplusplus
}
#endif
#endif /* SDGE_RENDERER_H */
