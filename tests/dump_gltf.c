/* dump_gltf.c - quick loader diagnostics: prints what the glTF loader
 * resolved for a given file (nodes, meshes, accessors, sample values).
 *
 *   gcc -I core tests/dump_gltf.c core/assets/assets.c core/assets/gltf.c
 */
#include <stdio.h>
#include "assets/gltf.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: dump_gltf <file.glb|file.gltf>\n");
        return 2;
    }
    sdge_gltf_model *m = sdge_load_gltf(argv[1]);
    if (!m) {
        fprintf(stderr, "load failed: %s\n", argv[1]);
        return 1;
    }
    printf("model %s\n", argv[1]);
    printf("  nodes=%d roots=%d meshes=%d materials=%d skins=%d anims=%d\n",
           m->node_count, m->root_count, m->mesh_count, m->material_count,
           m->skin_count, m->anim_count);

    for (int i = 0; i < m->mesh_count; ++i) {
        printf("mesh[%d] '%s' prims=%d\n", i,
               m->meshes[i].name ? m->meshes[i].name : "?",
               m->meshes[i].prim_count);
        for (int p = 0; p < m->meshes[i].prim_count; ++p) {
            sdge_primitive *pr = &m->meshes[i].prims[p];
            printf("  prim[%d] material=%d mode=%d\n", p, pr->material, pr->mode);
            printf("    pos: count=%d comps=%d ctype=%d stride=%d ptr=%p\n",
                   pr->positions.count, pr->positions.comps,
                   pr->positions.ctype, pr->positions.stride,
                   (void *)pr->positions.data);
            printf("    nrm: count=%d comps=%d ctype=%d stride=%d ptr=%p\n",
                   pr->normals.count, pr->normals.comps,
                   pr->normals.ctype, pr->normals.stride,
                   (void *)pr->normals.data);
            printf("    idx: count=%d comps=%d ctype=%d stride=%d ptr=%p\n",
                   pr->indices.count, pr->indices.comps,
                   pr->indices.ctype, pr->indices.stride,
                   (void *)pr->indices.data);
            if (pr->positions.count > 0 && pr->positions.ctype == SDGE_GLTF_FLOAT
                && pr->positions.comps == 3) {
                for (int v = 0; v < pr->positions.count && v < 4; ++v) {
                    const float *f = (const float *)
                        (pr->positions.data + (size_t)v
                         * (pr->positions.stride ? (size_t)pr->positions.stride
                                                : 12u));
                    printf("    v[%d] = (%.3f, %.3f, %.3f)\n", v,
                           f[0], f[1], f[2]);
                }
            }
            if (pr->indices.count > 0
                && pr->indices.ctype == SDGE_GLTF_UNSIGNED_SHORT) {
                for (int k = 0; k < pr->indices.count && k < 6; ++k) {
                    const unsigned char *p = pr->indices.data
                        + (size_t)k
                          * (pr->indices.stride ? (size_t)pr->indices.stride : 2u);
                    printf("    i[%d] = %d\n", k,
                           (int)(unsigned short)(p[0] | (p[1] << 8)));
                }
            }
        }
    }

    for (int i = 0; i < m->node_count && i < 6; ++i) {
        sdge_gltf_node *n = &m->nodes[i];
        printf("node[%d] '%s' parent=%d mesh=%d skin=%d children=%d "
               "t=(%.2f,%.2f,%.2f)\n",
               i, n->name ? n->name : "?", n->parent, n->mesh, n->skin,
               n->child_count, n->translation[0], n->translation[1],
               n->translation[2]);
    }
    for (int i = 0; i < m->material_count; ++i) {
        sdge_gltf_material *mt = &m->materials[i];
        printf("material[%d] '%s' base=(%.2f,%.2f,%.2f,%.2f) metal=%.2f "
               "rough=%.2f\n", i, mt->name ? mt->name : "?",
               mt->base_color[0], mt->base_color[1], mt->base_color[2],
               mt->base_color[3], mt->metallic, mt->roughness);
    }
    sdge_gltf_destroy(m);
    return 0;
}
