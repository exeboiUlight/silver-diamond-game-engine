/* mesh.h - Silver Diamond Game Engine :: GPU mesh (vertex/index buffers)
 *
 * Uploads a glTF primitive (see assets/gltf.h) into OpenGL objects once,
 * then draws it with one call. Data paths are optimized for the common case:
 * POSITION/NORMAL/WEIGHTS as float32 and indices as u16/u32 are uploaded
 * DIRECTLY from the loader's zero-copy accessors (no intermediate copy);
 * other component types are converted once at upload time.
 *
 * Skinning attributes are stored interleaved (joint, weight) x4 as float32,
 * so the vertex shader needs no integer divisors or normalization flags.
 */
#ifndef SDGE_MESH_H
#define SDGE_MESH_H

#include "../sdge_def.h"
#include "../math/math3d.h"
#include "../assets/gltf.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sdge_mesh {
    unsigned vao;            /* vertex array object, 0 when not uploaded */
    unsigned vbo_pos;
    unsigned vbo_nrm;
    unsigned vbo_skin;       /* joints+weights interleaved, 0 when unskinned */
    unsigned ebo;            /* 0 when the primitive is non-indexed */

    int   vertex_count;
    int   index_count;
    int   index_type;        /* 0 = drawArrays, 3 = GL_UNSIGNED_SHORT, 4 = u32 */
    int   has_skin;
    int   has_normals;
    int   double_sided;

    /* bounding sphere in MESH space (from POSITION accessor min/max) */
    float bounds_center[3];
    float bounds_radius;
} sdge_mesh;

/* 0 on success. Uploads one primitive; `prim` must outlive draws that use
 * no material? - only stats are copied, so the primitive may be freed. */
SDGE_API int  sdge_mesh_upload(sdge_mesh *out, const sdge_primitive *prim,
                               const sdge_gltf_material *material);
SDGE_API void sdge_mesh_destroy(sdge_mesh *m);

/* procedural meshes (for the physics bodies and the primitives) */
SDGE_API int  sdge_mesh_create_box(sdge_mesh *out, float hw, float hh, float hd);
SDGE_API int  sdge_mesh_create_sphere(sdge_mesh *out, float radius,
                                       int slices, int stacks);

/* bounding sphere transformed to world space (for frustum culling);
 * `scale_hint` is the max axis scale of the node matrix (caller extracts) */
SDGE_API void sdge_mesh_world_sphere(const sdge_mesh *m, const sdg_mat4 world,
                                     float center_out[3], float *radius_out);

#ifdef __cplusplus
}
#endif
#endif /* SDGE_MESH_H */
