/* anim.h - Silver Diamond Game Engine :: animation player + skeletal skinning
 *
 * Runtime side of the glTF animation pipeline. A player animates ONE
 * sdge_gltf_model with ONE animation:
 *
 *   sdge_anim_player *p = sdge_anim_player_create(model, 0);
 *   sdge_anim_player_set_looping(p, 1);
 *   for (;;) {
 *       sdge_anim_player_advance(p, dt);
 *       const float *joints = sdge_anim_player_skin_palette(p, skin);
 *       ... upload `skin->joint_count` mat4 uniforms ...
 *   }
 *
 * Optimizations (the sampling work is proportional to ANIMATED nodes only):
 *   - node channels are resolved once at player creation (no per-frame
 *     string/path matching);
 *   - a topological node order is precomputed, so the world-matrix pass is a
 *     linear loop with no recursion and no per-frame allocations;
 *   - matrices are recomputed lazily: setting the same time twice costs
 *     nothing; the skin palette reuses the cached world pass;
 *   - key lookup is a binary search over ascending times.
 *
 * Interpolations: LINEAR (lerp / quaternion slerp), STEP, CUBICSPLINE
 * (cubic Hermite with glTF's in/out tangent layout).
 */
#ifndef SDGE_ANIM_H
#define SDGE_ANIM_H

#include "../sdge_def.h"
#include "../math/math3d.h"
#include "../assets/gltf.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sdge_anim_player sdge_anim_player;

/* anim_index < 0 creates a "static" player: no channels, locals = bind pose */
SDGE_API sdge_anim_player *sdge_anim_player_create(const sdge_gltf_model *m,
                                                   int anim_index);
SDGE_API void  sdge_anim_player_destroy(sdge_anim_player *p);

SDGE_API void  sdge_anim_player_reset(sdge_anim_player *p);   /* time -> 0 */
SDGE_API void  sdge_anim_player_advance(sdge_anim_player *p, float dt);
SDGE_API void  sdge_anim_player_set_time(sdge_anim_player *p, float seconds);
SDGE_API float sdge_anim_player_get_time(const sdge_anim_player *p);
SDGE_API float sdge_anim_player_get_duration(const sdge_anim_player *p);
SDGE_API void  sdge_anim_player_set_looping(sdge_anim_player *p, int loop);
SDGE_API int   sdge_anim_player_is_looping(const sdge_anim_player *p);

/* local (parent space) matrix of node `node` after sampling */
SDGE_API void  sdge_anim_player_get_local(const sdge_anim_player *p,
                                          int node, sdg_mat4 out);
/* world matrix of node `node` (full chain) */
SDGE_API void  sdge_anim_player_get_world(const sdge_anim_player *p,
                                           int node, sdg_mat4 out);

/* skinning palette for `skin`: joint_count column-major mat4 =
 * world(joint) * inverseBindMatrix, ready for glUniformMatrix4fv. */
SDGE_API const float *sdge_anim_player_skin_palette(const sdge_anim_player *p,
                                                     const sdge_gltf_skin *skin);

#ifdef __cplusplus
}
#endif
#endif /* SDGE_ANIM_H */
