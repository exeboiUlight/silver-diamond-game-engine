/* sdge.h - Silver Diamond Game Engine :: public umbrella header
 *
 * Include this one header from game code:
 *     #include "sdge.h"
 *
 * Modules (all exported from sdge.dll / libsdge.so):
 *   - window layer      graphics/window.h   (Win32/WGL, X11/GLX)
 *   - 3D math           math/math3d.h       (vec/quat/mat, frustum)
 *   - assets            assets/assets.h     (arena, files, WAV)
 *   - glTF 2.0 loader   assets/gltf.h       (.glb / .gltf+.bin)
 *   - animation+skin    anim/anim.h         (sampling, joint palettes)
 *   - spatial audio     audio/audio.h       (48 kHz mixer, waveOut/ALSA)
 *   - renderer          graphics/renderer.h (GL 3.3+, PBR-lite)
 *   - GPU meshes        graphics/mesh.h
 *   - 3D subjects       subjects/3D/...     (model, camera, lights)
 *   - 2D UI (shader)    graphics/ui.h       (SDF boxes + font atlas)
 *   - GUI               subjects/2D/gui/... (div, button, input, label,
 *                                            checkbox, progress, slider,
 *                                            radio, listbox)
 *   - 2D camera         subjects/2D/Camera.h
 *   - physics           physics/physics2d.h  (circles + OBB, impulses)
 *                        physics/physics3d.h  (spheres + OBB, impulses)
 *   - scenes            scene/scene.h       (manager + fade transitions)
 *
 * Static/embedded use (no DLL): compile the engine .c files listed in
 * core/config.py directly into the program instead.
 */
#ifndef SDGE_H
#define SDGE_H

#include "sdge_def.h"

#include "graphics/window.h"

#include "math/math3d.h"

#include "assets/assets.h"
#include "assets/gltf.h"
#include "assets/mp3.h"

#include "anim/anim.h"

#include "audio/audio.h"

#include "graphics/mesh.h"
#include "graphics/renderer.h"
#include "graphics/ui.h"

#include "subjects/3D/Camera.h"
#include "subjects/3D/model.h"
#include "subjects/3D/light/AmbientLight.h"
#include "subjects/3D/light/PointLight.h"
#include "subjects/3D/light/Sun.h"

#include "subjects/2D/Camera.h"
#include "subjects/2D/gui/gui.h"
#include "subjects/2D/gui/div.h"
#include "subjects/2D/gui/button.h"
#include "subjects/2D/gui/input.h"
#include "subjects/2D/gui/label.h"
#include "subjects/2D/gui/checkbox.h"
#include "subjects/2D/gui/progress.h"
#include "subjects/2D/gui/slider.h"
#include "subjects/2D/gui/radio.h"
#include "subjects/2D/gui/listbox.h"

#include "physics/physics2d.h"
#include "physics/physics3d.h"

#include "scene/scene.h"

#endif /* SDGE_H */
