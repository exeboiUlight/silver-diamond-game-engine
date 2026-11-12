/* scene.h - Silver Diamond Game Engine :: scene management
 *
 * A scene is a plain struct with callbacks; the manager owns the list,
 * switches between the scenes and runs the fade transition (the fade is a
 * full screen quad drawn by the UI shader, so it costs nothing extra).
 *
 * Typical use:
 *     sdge_scene_manager mgr;
 *     sdge_scene_mgr_init(&mgr);
 *     sdge_scene_mgr_add(&mgr, &menu);
 *     sdge_scene_mgr_add(&mgr, &world);
 *     sdge_scene_mgr_switch_now(&mgr, 0);
 *
 *     // per frame
 *     sdge_scene_mgr_update(&mgr, dt);
 *     sdge_scene_mgr_render(&mgr);        // the 3D part of the scene
 *     sdge_gui_begin(gui, w, h);
 *     sdge_scene_mgr_render_gui(&mgr, gui);
 *     sdge_gui_end(gui);
 */
#ifndef SDGE_SCENE_H
#define SDGE_SCENE_H

#include "../sdge_def.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SDGE_SCENE_MAX 32

/* scene event types (see sdge_scene_manager_event) */
#define SDGE_SCENE_EVENT_KEY      1     /* param = the key code    */
#define SDGE_SCENE_EVENT_CHAR     2     /* param = the codepoint  */
#define SDGE_SCENE_EVENT_CLICK    3
#define SDGE_SCENE_EVENT_RESIZE   4     /* param = the width       */
#define SDGE_SCENE_EVENT_CUSTOM   100   /* param = the user value  */

struct sdge_gui;

typedef struct sdge_scene {
    const char *name;

    void (*on_enter)(struct sdge_scene *s);
    void (*on_exit)(struct sdge_scene *s);
    void (*on_update)(struct sdge_scene *s, float dt);
    void (*on_render)(struct sdge_scene *s);                 /* the world */
    void (*on_gui)(struct sdge_scene *s, struct sdge_gui *gui); /* the UI  */
    void (*on_event)(struct sdge_scene *s, int type, int param);

    void *user;            /* the application data */
} sdge_scene;

typedef struct sdge_scene_manager {
    sdge_scene *scenes[SDGE_SCENE_MAX];
    int count;
    int current;           /* the index of the live scene */
    int next;              /* the scene we are fading towards */
    int entered;           /* the live scene has been entered already */

    float fade;            /* 0 = clear, 1 = covered */
    int   fade_dir;        /* 0 idle, -1 fading out, +1 fading in */
    float fade_speed;      /* the fade duration in seconds */
    float fade_delay;      /* the pause between the two halves */
} sdge_scene_manager;

/* the fade is a solid colour over the whole screen */
SDGE_API void sdge_scene_mgr_set_fade_color(sdge_scene_manager *m,
                                             const float rgba[4]);

SDGE_API void sdge_scene_mgr_init(sdge_scene_manager *m);
SDGE_API int  sdge_scene_mgr_add(sdge_scene_manager *m, sdge_scene *s);

/* switches with a fade transition */
SDGE_API void sdge_scene_mgr_switch(sdge_scene_manager *m, int index);

/* switches immediately (no transition) */
SDGE_API void sdge_scene_mgr_switch_now(sdge_scene_manager *m, int index);

SDGE_API void sdge_scene_mgr_update(sdge_scene_manager *m, float dt);
SDGE_API void sdge_scene_mgr_render(sdge_scene_manager *m);
SDGE_API void sdge_scene_mgr_render_gui(sdge_scene_manager *m,
                                        struct sdge_gui *gui);
SDGE_API void sdge_scene_mgr_event(sdge_scene_manager *m, int type,
                                   int param);

SDGE_API sdge_scene *sdge_scene_mgr_current(sdge_scene_manager *m);
SDGE_API int sdge_scene_mgr_transitioning(const sdge_scene_manager *m);
SDGE_API float sdge_scene_mgr_fade(const sdge_scene_manager *m);

#ifdef __cplusplus
}
#endif
#endif /* SDGE_SCENE_H */
