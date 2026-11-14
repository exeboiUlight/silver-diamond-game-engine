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

/* ------------------------------------------------------------- hierarchy
 * The scene editor moves the single nodes of a model, so the model subject
 * has to expose them.  Every node of the glTF file is one item here. */
SDGE_API int sdge_model_node_count(const sdge_model *m);
SDGE_API const char *sdge_model_node_name(const sdge_model *m, int node);
SDGE_API int sdge_model_node_parent(const sdge_model *m, int node);
SDGE_API int sdge_model_node_mesh(const sdge_model *m, int node);
SDGE_API int sdge_model_node_skin(const sdge_model *m, int node);
/* the children of a node (the indices are written to `out`); the count */
SDGE_API int sdge_model_node_children(const sdge_model *m, int node,
                                      int *out, int max);

/* the local transform of a node (the TRS of the glTF file); the pointers
 * may be NULL if a part of the transform is not needed */
SDGE_API void sdge_model_node_get_transform(const sdge_model *m, int node,
                                            float *t, float *r, float *s);
/* moves a node: the change is written into the glTF data and the world
 * matrices are rebuilt (call sdge_model_refresh once after a batch of
 * changes) */
SDGE_API void sdge_model_node_set_transform(sdge_model *m, int node,
                                            const sdg_vec3 t,
                                            const sdg_quat r,
                                            const sdg_vec3 s);
/* the world matrix of a node (parent chain included) */
SDGE_API void sdge_model_node_world(const sdge_model *m, int node,
                                    sdg_mat4 out);
/* the world space bounding box of the node (the mesh of the node) */
SDGE_API void sdge_model_node_bounds(const sdge_model *m, int node,
                                     float min_out[3], float max_out[3]);
/* rebuilds the world matrices of every node (after the transforms changed) */
SDGE_API void sdge_model_refresh(sdge_model *m);

/* -------------------------------------------------------------- material */
/* the material of one primitive of the node (-1 = none) */
SDGE_API int sdge_model_node_material(const sdge_model *m, int node, int prim);
/* the number of the primitives of the node */
SDGE_API int sdge_model_node_prim_count(const sdge_model *m, int node);
/* overrides one texture slot of a node (SDGR_SLOT_*); 0 removes the
 * override and the material texture is used again */
SDGE_API int sdge_model_set_node_texture(sdge_model *m, int node, int slot,
                                          unsigned texture);
SDGE_API unsigned sdge_model_get_node_texture(const sdge_model *m, int node,
                                               int slot);

#ifdef __cplusplus
}
#endif
#endif /* SDGE_MODEL3D_H */
