/* editor.h - Silver Diamond Game Engine :: scene editor
 *
 * The editor opens a glTF/GLB file, shows its node hierarchy and lets the
 * user move, rotate and scale the single nodes of the model, put a texture
 * on them and save the result as a new glTF file.
 *
 * The editor is a normal engine module: it draws the model with the
 * renderer, picks the nodes with the mouse and keeps the changes inside the
 * glTF data of the model (so the export can write them out).
 *
 *   sdge_editor *ed = sdge_editor_create();
 *   sdge_editor_open(ed, "Subjects/character.glb");
 *
 *   while (running) {
 *       sdge_poll_events();
 *       sdgr_begin_frame(clear);
 *       sdge_editor_render(ed);
 *       sdgr_end_frame();
 *   }
 */
#ifndef SDGE_EDITOR_H
#define SDGE_EDITOR_H

#include "../sdge_def.h"
#include "../math/math3d.h"
#include "../subjects/3D/Camera.h"
#include "../subjects/3D/model.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sdge_editor sdge_editor;   /* opaque */

/* the tools of the viewport */
#define SDGE_EDIT_SELECT 0    /* pick a node */
#define SDGE_EDIT_MOVE   1    /* drag the node */
#define SDGE_EDIT_ROTATE 2    /* turn the node */
#define SDGE_EDIT_SCALE  3    /* scale the node */

/* the axis constraint of the tool (0 = free) */
#define SDGE_EDIT_AXIS_NONE 0
#define SDGE_EDIT_AXIS_X    1
#define SDGE_EDIT_AXIS_Y    2
#define SDGE_EDIT_AXIS_Z    3

/* the texture slots (the same order as the renderer) */
#define SDGE_EDIT_TEX_BASE      0
#define SDGE_EDIT_TEX_METALLIC  1
#define SDGE_EDIT_TEX_NORMAL    2
#define SDGE_EDIT_TEX_OCCLUSION 3
#define SDGE_EDIT_TEX_EMISSIVE  4

/* one row of the hierarchy */
typedef struct {
    int  node;            /* the index of the glTF node */
    int  depth;           /* the depth in the tree */
    int  has_mesh;
    char name[96];
} sdge_edit_item;

/* --------------------------------------------------------------- the life */
SDGE_API sdge_editor *sdge_editor_create(void);
SDGE_API void sdge_editor_destroy(sdge_editor *ed);

/* opens a model (0 = ok); the editor takes the model over */
SDGE_API int  sdge_editor_open(sdge_editor *ed, const char *path);
SDGE_API void sdge_editor_close(sdge_editor *ed);
SDGE_API const char *sdge_editor_path(const sdge_editor *ed);
SDGE_API sdge_model *sdge_editor_model(const sdge_editor *ed);

/* --------------------------------------------------------- the hierarchy */
SDGE_API int sdge_editor_item_count(const sdge_editor *ed);
SDGE_API int sdge_editor_item_node(const sdge_editor *ed, int item);
SDGE_API const char *sdge_editor_item_name(const sdge_editor *ed, int item);
SDGE_API int sdge_editor_item_depth(const sdge_editor *ed, int item);
SDGE_API int sdge_editor_item_has_mesh(const sdge_editor *ed, int item);

/* the selection (an item index, -1 = nothing is selected) */
SDGE_API void sdge_editor_select(sdge_editor *ed, int item);
SDGE_API int  sdge_editor_selected(const sdge_editor *ed);
SDGE_API int  sdge_editor_selected_node(const sdge_editor *ed);

/* the tools */
SDGE_API void sdge_editor_set_tool(sdge_editor *ed, int tool);
SDGE_API int  sdge_editor_tool(const sdge_editor *ed);
SDGE_API void sdge_editor_set_axis(sdge_editor *ed, int axis);
SDGE_API int  sdge_editor_axis(const sdge_editor *ed);

/* the local transform of the selected node */
SDGE_API int sdge_editor_get_transform(sdge_editor *ed, sdg_vec3 t,
                                       sdg_quat r, sdg_vec3 s);
SDGE_API int sdge_editor_set_transform(sdge_editor *ed, const sdg_vec3 t,
                                       const sdg_quat r, const sdg_vec3 s);

/* the camera of the viewport */
SDGE_API sdge_camera3d *sdge_editor_camera(sdge_editor *ed);
SDGE_API void sdge_editor_orbit(sdge_editor *ed, float yaw, float pitch,
                                 float dist);
SDGE_API void sdge_editor_focus(sdge_editor *ed);   /* frames the model */
SDGE_API void sdge_editor_update_camera(sdge_editor *ed, int width,
                                        int height);

/* the viewport rectangle (the mouse coordinates are relative to it) */
SDGE_API void sdge_editor_set_viewport(sdge_editor *ed, int x, int y,
                                       int w, int h);

/* the input of the viewport */
SDGE_API void sdge_editor_mouse_move(sdge_editor *ed, float x, float y);
SDGE_API void sdge_editor_mouse_button(sdge_editor *ed, int button,
                                       int action);
SDGE_API void sdge_editor_scroll(sdge_editor *ed, float dy);
/* the tool shortcuts: 1 = select, 2 = move, 3 = rotate, 4 = scale,
 * x/y/z = the axis constraint, escape = the free axis */
SDGE_API void sdge_editor_key(sdge_editor *ed, int key);

/* the node under the mouse (-1 = nothing) */
SDGE_API int sdge_editor_pick(sdge_editor *ed, float x, float y);

/* draws the model and the gizmos of the selected node */
SDGE_API void sdge_editor_render(sdge_editor *ed);

/* ---------------------------------------------------------- the textures */
/* puts a texture on a node (the file is decoded with stb_image and uploaded
 * to the GPU); 0 = ok.  `slot` is one of SDGE_EDIT_TEX_* */
SDGE_API int sdge_editor_apply_texture(sdge_editor *ed, int item,
                                        const char *path, int slot);
/* removes the override of the slot (the material texture is used again) */
SDGE_API int sdge_editor_clear_texture(sdge_editor *ed, int item, int slot);
/* the file of the texture that was put on a node (NULL = none) */
SDGE_API const char *sdge_editor_texture_path(sdge_editor *ed, int node,
                                               int slot);

/* ------------------------------------------------------------- the export */
/* writes the edited model as a glTF file (the binary data goes into
 * <name>.bin, the textures are embedded as data URIs); 0 = ok */
SDGE_API int sdge_editor_export(sdge_editor *ed, const char *path);
/* the message of the last error (an empty string = no error) */
SDGE_API const char *sdge_editor_error(const sdge_editor *ed);

#ifdef __cplusplus
}
#endif
#endif /* SDGE_EDITOR_H */
