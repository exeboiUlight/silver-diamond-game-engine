/* renderer.h - Silver Diamond Game Engine :: forward renderer (OpenGL 3.3+)
 *
 * One shader for everything skinned/unskinned, PBR-lite lighting:
 * ambient + one directional "sun" + up to 8 point lights + optional
 * baseColor texture (sampler2D). The renderer keeps ALL GL state internal
 * (glad is a private dependency, loaded once inside sdgr_init() -
 * applications never touch GL headers).
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

SDGE_API int  sdgr_init(void);
SDGE_API void sdgr_shutdown(void);

SDGE_API void sdgr_resize(int width, int height);
SDGE_API void sdgr_begin_frame(const float clear_color[4]);
SDGE_API void sdgr_end_frame(void);

SDGE_API void sdgr_set_camera(const sdg_mat4 view, const sdg_mat4 proj,
                              const float cam_pos[3]);
SDGE_API int  sdgr_sphere_visible(const float center[3], float radius);

SDGE_API void sdgr_set_ambient(float r, float g, float b);
SDGE_API void sdgr_set_sun(const float dir[3], const float color[3],
                           float intensity);
SDGE_API void sdgr_set_point_light(int index, const float pos[3],
                                   const float color[3],
                                   float intensity, float radius);

/* NEW: texture upload/free (RGBA8) - the caller owns the result */
SDGE_API unsigned sdgr_upload_texture(int w, int h,
                                      const unsigned char *rgba8);
SDGE_API void     sdgr_free_texture(unsigned tex);

/* `base_texture` = 0 means "use base_color only". The texture is sampled
 * with the mesh's TEXCOORD_0; if the mesh has none, the texture is ignored. */
SDGE_API void sdgr_draw_mesh(const sdge_mesh *mesh, const sdg_mat4 model,
                             const float *joints, int joint_count,
                             const float base_color[4],
                             float metallic, float roughness, int double_sided,
                             unsigned base_texture);

SDGE_API int  sdgr_screenshot(const char *path);

#ifdef __cplusplus
}
#endif
#endif /* SDGE_RENDERER_H */