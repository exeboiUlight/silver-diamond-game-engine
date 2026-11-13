/* model.h - Silver Diamond Game Engine :: 3D model subject (glTF)
 *
 * The high-level "3D object": loads a glTF file (.glb or .gltf+.bin),
 * uploads all meshes to the GPU, owns animation players and draws the
 * whole node hierarchy with skinning and frustum culling.
 *
 *   sdge_model *m = sdge_model_load("Subjects/character.glb");
 *   sdge_model_play(m, 0);                    // animation 0, looping
 *   ...
 *   sdge_model_advance(m, dt);
 *   sdge_model_draw(m, model_matrix);
 *
 * - skinned meshes use the animation player's joint palette
 *   (world * inverseBindMatrix per joint);
 * - static (unanimated) meshes use world matrices computed once at load;
 * - every primitive is culled with its bounding sphere against the camera
 *   frustum that sdgr_set_camera() installed.
 */
#ifndef SDGE_MODEL3D_H
#define SDGE_MODEL3D_H

#include "../../sdge_def.h"
#include "../../math/math3d.h"
#include "../../assets/gltf.h"
#include "../../graphics/mesh.h"
#include "../../anim/anim.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sdge_model sdge_model;   /* opaque */

/* NULL on failure (missing file, bad glTF, or no GL in this build) */
SDGE_API sdge_model *sdge_model_load(const char *path);
SDGE_API void sdge_model_destroy(sdge_model *m);

/* animations (indices as in the glTF file); returns the count */
SDGE_API int  sdge_model_anim_count(const sdge_model *m);
SDGE_API const char *sdge_model_anim_name(const sdge_model *m, int anim);
SDGE_API void sdge_model_play(sdge_model *m, int anim_index, int loop);
SDGE_API void sdge_model_stop(sdge_model *m);
SDGE_API void sdge_model_advance(sdge_model *m, float dt);
SDGE_API void sdge_model_set_time(sdge_model *m, float seconds);

/* the raw glTF data behind the model (nodes, materials, textures, ...) */
SDGE_API const sdge_gltf_model *sdge_model_gltf(const sdge_model *m);

/* draw the whole hierarchy; `model` = additional parent transform
 * (NULL = identity). Uses the renderer's camera frustum for culling. */
SDGE_API void sdge_model_draw(sdge_model *m, const sdg_mat4 model);

#ifdef __cplusplus
}
#endif
#endif /* SDGE_MODEL3D_H */
