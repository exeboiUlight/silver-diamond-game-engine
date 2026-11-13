/* mesh.h - Silver Diamond Game Engine :: GPU mesh (vertex/index buffers)
 *
 * Uploads a glTF primitive (see assets/gltf.h) into OpenGL objects once,
 * then draws it with one call. Data paths are optimized for the common case:
 * POSITION/NORMAL/TEXCOORD_0/WEIGHTS as float32 and indices as u16/u32 are
 * uploaded DIRECTLY from the loader's zero-copy accessors (no intermediate
 * copy); other component types are converted once at upload time.
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
    unsigned vbo_tex;        /* TEXCOORD_0, 0 when the primitive has none */
    unsigned vbo_tex1;       /* TEXCOORD_1 */
    unsigned vbo_tan;        /* TANGENT (xyz + handedness) */
    unsigned vbo_skin;       /* joints+weights interleaved, 0 when unskinned */
    unsigned ebo;            /* 0 when the primitive is non-indexed */

    int   vertex_count;
    int   index_count;
    int   index_type;        /* 0 = drawArrays, 3 = GL_UNSIGNED_SHORT, 4 = u32 */
    int   has_skin;
    int   has_normals;
    int   has_texcoords;
    int   has_texcoords2;    /* TEXCOORD_1 present */
    int   has_tangents;
    int   double_sided;

    float bounds_center[3];
    float bounds_radius;
} sdge_mesh;

SDGE_API int  sdge_mesh_upload(sdge_mesh *out, const sdge_primitive *prim,
                               const sdge_gltf_material *material);
SDGE_API void sdge_mesh_destroy(sdge_mesh *m);

SDGE_API int  sdge_mesh_create_box(sdge_mesh *out, float hw, float hh, float hd);
SDGE_API int  sdge_mesh_create_sphere(sdge_mesh *out, float radius,
                                       int slices, int stacks);

SDGE_API void sdge_mesh_world_sphere(const sdge_mesh *m, const sdg_mat4 world,
                                     float center_out[3], float *radius_out);

#ifdef __cplusplus
}
#endif
#endif /* SDGE_MESH_H */