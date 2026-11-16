/* shader_lang.h - Silver Diamond Game Engine :: simple shader language
 *
 * Internal interface between the public renderer API (renderer.c) and the
 * small custom-shading language translator (shader_lang.c).  Applications
 * never include this header: they use the opaque `sdgr_shader` API from
 * graphics/renderer.h.
 *
 * The language is a deliberately small, GLSL-like DSL:
 *
 *     shader glow {
 *         uniform float intensity;
 *
 *         vertex {
 *             position.y += sin(time + position.x * 4.0) * 0.04;
 *         }
 *
 *         fragment {
 *             vec3 lit = pbrLighting(normal, albedo, metallic, roughness);
 *             color.rgb = mix(lit, vec3(0.2, 0.8, 1.0), intensity);
 *         }
 *     }
 *
 * The translator emits a complete GLSL 330 vertex/fragment pair that shares
 * the built-in PBR vertex transform and the surface values of the forward
 * renderer (base color, normal map, metallic/roughness, textures, lights).
 */
#ifndef SDGE_SHADER_LANG_H
#define SDGE_SHADER_LANG_H

#define SDSL_MAX_UNIFORMS 32
#define SDSL_NAME_MAX     64

enum {
    SDSL_UNIFORM_FLOAT = 1,
    SDSL_UNIFORM_INT,
    SDSL_UNIFORM_BOOL,
    SDSL_UNIFORM_VEC2,
    SDSL_UNIFORM_VEC3,
    SDSL_UNIFORM_VEC4,
    SDSL_UNIFORM_SAMPLER2D
};

typedef struct {
    char name[SDSL_NAME_MAX];
    int  type;                  /* SDSL_UNIFORM_* */
} sdsl_uniform;

typedef struct {
    char *vertex;               /* malloc'd complete GLSL vertex shader    */
    char *fragment;             /* malloc'd complete GLSL fragment shader  */
    sdsl_uniform uniforms[SDSL_MAX_UNIFORMS];
    int uniform_count;
} sdsl_program;

/* Translates the DSL source.  Returns 0 on success and fills `out`;
 * returns -1 on failure and writes a human-readable message to `error`.
 * On failure `out` is left zeroed and must not be freed. */
int  sdsl_compile(const char *source, sdsl_program *out,
                  char *error, int error_size);
void sdsl_program_free(sdsl_program *p);

#endif /* SDGE_SHADER_LANG_H */
