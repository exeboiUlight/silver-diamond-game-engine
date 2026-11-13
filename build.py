"""
Author - ExeBoi
Date - 10.11.2026

this build script makes for "Silver Diamond Game Engine"

The engine builds as a DYNAMIC LIBRARY and the game links against it:
    build/<platform>/<variant>/sdge.dll     (Windows, + libsdge.a import lib)
    build/<platform>/<variant>/libsdge.so    (Linux/WSL)
    build/<platform>/<variant>/<name>.exe    the game executable

Usage:
    python build.py               build the engine DLL + app (incremental)
    python build.py run           build, then run the app
    python build.py rebuild       clean + build
    python build.py clean         remove the whole build/ directory
    python build.py sdk           build the engine and export the C SDK to API/
    python build.py demo --demo NAME
                                  build demos/NAME.c into an executable

Options:
    --debug         build with -O0 -g (default is release: -O2 -DNDEBUG)
    --nogl          build without OpenGL (-DSDGE_NO_GL, no GL libs)
    --name NAME     binary name (default: `name` from src/config.py or "silver_diamond")
    --wsl           build inside WSL instead of Windows (only usable from Windows)

Source lists live in python configs (add files there, not here):
    core/config.py : config = [...]   ENGINE sources, paths relative to core/
    src/config.py  : config = [...]   APPLICATION sources, paths relative to src/
                                      (optional: name = "my_game" sets the binary name)
    include/glad   : OpenGL loader, compiled into the engine DLL only
                     (games never call GL directly - sdgr_init() loads it)

Output of build (split by host platform AND variant, so a Windows build
never clashes with a WSL build of the same project directory):
    build/<platform>/<variant>/obj/*.o + *.d        objects + dependency files
    build/<platform>/<variant>/API/                 copied SDK
    <platform> is "windows" or "linux", <variant> is release|release_nogl|debug|...

Incremental rules: an object is rebuilt when its source file changes OR when
any header it includes changes (tracked via gcc -MMD -MP dependency files);
the DLL is relinked when any engine object changed, the app when any object
or the DLL changed.
"""

import os
import re
import sys
import shlex
import shutil
import argparse
import subprocess

import core.config as co1
import src.config as co2

ROOT = os.path.dirname(os.path.abspath(__file__))


class Main:
    def __init__(self, args):
        # module configs: engine sources (core/) and app sources (src/)
        self.engine_mod, self.engine_list = co1, co1.config
        self.app_mod, self.app_list = co2, co2.config
        # third-party C sources compiled into the ENGINE library (rel. to ROOT)
        self.lib_c_files = ["include/glad/glad.c", "include/minimp3/minimp3.c"]

        self.args = args
        self.is_windows = (os.name == "nt")
        self.cc = self.find_compiler()
        self.name = args.name or getattr(co2, "name", None) or "silver_diamond"

        # build dirs are split per HOST PLATFORM as well: the project folder
        # can be shared by Windows and WSL (e.g. /mnt/d/...), and objects of
        # one platform must never be reused by the other's linker
        self.platform = "windows" if self.is_windows else "linux"
        self.variant = ("debug" if args.debug else "release") + \
                       ("_nogl" if args.nogl else "")

        self.build_dir = os.path.join("build", self.platform, self.variant)
        self.obj_dir = os.path.join(self.build_dir, "obj")
        self.out = os.path.join(self.build_dir,
                                self.name + (".exe" if self.is_windows else ""))

        # engine shared library
        if self.is_windows:
            self.dll = os.path.join(self.build_dir, "sdge.dll")
            self.implib = os.path.join(self.build_dir, "libsdge.a")
        else:
            self.dll = os.path.join(self.build_dir, "libsdge.so")
            self.implib = None

        # ALSA is optional on Linux (WSL images may lack it)
        self.has_asound = (not self.is_windows) and \
            os.path.exists("/usr/include/alsa/asoundlib.h")

        self.sdk_headers = [
            "sdge_def.h",
            "graphics/window.h",
            "math/math3d.h",
            "assets/assets.h",
            "assets/gltf.h",
            "assets/json.h",
            "assets/mp3.h",
            "assets/font.h",
            "assets/texture.h",
            "anim/anim.h",
            "audio/audio.h",
            "graphics/mesh.h",
            "graphics/renderer.h",
            "graphics/ui.h",
            "subjects/3D/Camera.h",
            "subjects/3D/model.h",
            "subjects/3D/light/AmbientLight.h",
            "subjects/3D/light/PointLight.h",
            "subjects/3D/light/Sun.h",
            "subjects/2D/Camera.h",
            "subjects/2D/gui/gui.h",
            "subjects/2D/gui/div.h",
            "subjects/2D/gui/button.h",
            "subjects/2D/gui/input.h",
            "subjects/2D/gui/label.h",
            "subjects/2D/gui/checkbox.h",
            "subjects/2D/gui/progress.h",
            "subjects/2D/gui/slider.h",
            "subjects/2D/gui/radio.h",
            "subjects/2D/gui/listbox.h",
            "physics/physics2d.h",
            "physics/physics3d.h",
            "scene/scene.h",
        ]

    # ------------------------------------------------------------- toolchain
    def find_compiler(self):
        if self.is_windows:
            for cand in (r"C:\MinGW\bin\gcc.exe", "gcc", "clang"):
                p = shutil.which(cand)
                if p:
                    return p
            return None
        for cand in ("gcc", "clang", "cc"):
            p = shutil.which(cand)
            if p:
                return p
        return None

    def compile_flags(self, third_party=False, engine=False):
        f = ["-std=c99" if self.is_windows else "-std=gnu99"]
        if third_party:            # vendored generated code: keep output clean
            f.append("-w")
        else:
            f += ["-Wall", "-Wextra"]
        if engine:
            f.append("-DSDGE_BUILD_DLL")
        else:
            f.append("-DSDGE_USE_DLL")
        if not self.is_windows and engine:
            f.append("-fPIC")
        if self.args.nogl:
            f.append("-DSDGE_NO_GL")
        if not self.is_windows and not self.has_asound:
            f.append("-DSDGE_NO_ALSA")
        if self.args.debug:
            f += ["-O0", "-g"]
        else:
            f += ["-O2", "-DNDEBUG"]
        return f

    def include_dirs(self):
        return ["include", "core", "core/graphics", "src"]

    def dll_link_libs(self):
        """libs the engine DLL itself must be linked with"""
        if self.is_windows:
            libs = ["-lgdi32", "-luser32", "-lwinmm"]
            if not self.args.nogl:
                libs.append("-lopengl32")
            return libs
        libs = ["-lX11", "-lm", "-lpthread"]
        if not self.args.nogl:
            libs.append("-lGL")
        if self.has_asound:
            libs.append("-lasound")
        return libs

    # ------------------------------------------------------------- sources
    def sources_of(self, mod, lst, third_party_extra=()):
        """[(relative source path, third_party?)] for one module config."""
        base = os.path.dirname(mod.__file__)          # e.g. .../core, .../src
        out = []
        for s in lst:
            rel = os.path.relpath(os.path.join(base, s), ROOT).replace("\\", "/")
            out.append((rel, False))
        for s in third_party_extra:
            out.append((s.replace("\\", "/"), True))
        return out

    def engine_sources(self):
        return self.sources_of(self.engine_mod, self.engine_list, self.lib_c_files)

    def app_sources(self):
        return self.sources_of(self.app_mod, self.app_list)

    def obj_of(self, src):
        # "src/main.c" -> build/<platform>/<variant>/obj/src_main.c.o
        return os.path.join(self.obj_dir,
                            re.sub(r"[^A-Za-z0-9._-]", "_", src) + ".o")

    @staticmethod
    def abspath(rel):
        return os.path.join(ROOT, rel)

    # ------------------------------------------------------------- incremental
    def is_stale(self, src, obj):
        """True when obj must be recompiled (source or any included header newer)."""
        obj_abs = self.abspath(obj)
        if not os.path.exists(obj_abs):
            return True
        obj_time = os.path.getmtime(obj_abs)
        if os.path.getmtime(self.abspath(src)) > obj_time:
            return True

        dep = obj[:-2] + ".d"                         # .o -> .d
        try:
            with open(self.abspath(dep), "r", encoding="utf-8",
                      errors="replace") as f:
                text = f.read()
        except OSError:
            return True                               # no dep info -> be safe
        if ":" in text:
            body = text.split(":", 1)[1].replace("\\\n", " ")
            for tok in body.split()[1:]:              # first token is the target
                p = tok if os.path.isabs(tok) else self.abspath(tok)
                if os.path.exists(p) and os.path.getmtime(p) > obj_time:
                    return True
        return False

    # ------------------------------------------------------------- steps
    def compile(self, src, third_party, engine):
        obj = self.obj_of(src)
        dep = obj[:-2] + ".d"
        cmd = [self.cc, *self.compile_flags(third_party, engine)]
        for d in self.include_dirs():
            cmd += ["-I", d]
        cmd += ["-MMD", "-MP", "-MF", dep, "-c", src, "-o", obj]
        print("[build] cc:", " ".join(shlex.quote(c) for c in cmd))
        r = subprocess.run(cmd, capture_output=True, text=True,
                           encoding="utf-8", errors="replace", cwd=ROOT)
        if r.returncode != 0:
            print(r.stdout)
            print(r.stderr)
            print(f"[build] FAILED to compile: {src}")
            return False
        if r.stderr.strip():
            print(r.stderr.rstrip())
        return True

    def link_dll(self, objs):
        cmd = [self.cc, "-shared", *objs, "-o", self.dll, *self.dll_link_libs()]
        if self.is_windows:
            cmd += [f"-Wl,--out-implib,{self.implib}"]
        print("[build] ld:", " ".join(shlex.quote(c) for c in cmd))
        r = subprocess.run(cmd, capture_output=True, text=True,
                           encoding="utf-8", errors="replace", cwd=ROOT)
        if r.returncode != 0:
            print(r.stdout)
            print(r.stderr)
            print(f"[build] FAILED to link {self.dll}")
            return False
        if r.stderr.strip():
            print(r.stderr.rstrip())
        return True

    def link_app(self, objs, out=None):
        target = out or self.out
        if self.is_windows:
            cmd = [self.cc, *objs, "-o", target, self.implib]
        else:
            # rpath $ORIGIN: the exe finds libsdge.so next to itself
            # -lm: gcc folds sinf+cosf into sincosf(), which lives in libm
            cmd = [self.cc, *objs, "-o", target,
                   f"-L{self.build_dir}", "-lsdge", "-Wl,-rpath,$ORIGIN", "-lm"]
        print("[build] ld:", " ".join(shlex.quote(c) for c in cmd))
        r = subprocess.run(cmd, capture_output=True, text=True,
                           encoding="utf-8", errors="replace", cwd=ROOT)
        if r.returncode != 0:
            print(r.stdout)
            print(r.stderr)
            print(f"[build] FAILED to link {target}")
            return False
        if r.stderr.strip():
            print(r.stderr.rstrip())
        return True

    # ------------------------------------------------------------- commands
    @staticmethod
    def any_newer(targets, sources):
        """True when a target is missing or any source is newer than it."""
        for t in targets:
            t_abs = Main.abspath(t)
            if not os.path.exists(t_abs):
                return True
            t_time = os.path.getmtime(t_abs)
            for s in sources:
                s_abs = Main.abspath(s)
                if os.path.exists(s_abs) and os.path.getmtime(s_abs) > t_time:
                    return True
        return False

    def build(self, build_app=True):
        if not self.cc:
            where = " (looked in C:\\MinGW\\bin and PATH)" if self.is_windows \
                else " (looked for gcc/clang/cc in PATH)"
            print(f"[build] ERROR: no C compiler found{where}")
            return False

        engine = self.engine_sources()
        app = self.app_sources()
        sources = engine + (app if build_app else [])
        missing = [s for s, _ in sources if not os.path.isfile(self.abspath(s))]
        if missing:
            print("[build] ERROR: sources listed in config but not on disk:")
            for s in missing:
                print("    -", s)
            return False

        os.makedirs(self.abspath(self.obj_dir), exist_ok=True)

        def compile_all(sources, is_engine):
            compiled = 0
            for src, third_party in sources:
                obj = self.obj_of(src)
                if self.is_stale(src, obj):
                    if not self.compile(src, third_party, is_engine):
                        return None
                    compiled += 1
            return compiled

        # ---- engine DLL ----
        compiled_engine = compile_all(engine, is_engine=True)
        if compiled_engine is None:
            return False
        engine_objs = [self.obj_of(s) for s, _ in engine]
        dll_targets = [self.dll] + ([self.implib] if self.implib else [])
        if compiled_engine > 0 or self.any_newer(dll_targets, engine_objs):
            if not self.link_dll(engine_objs):
                return False
            print(f"[build] built {self.dll} "
                  f"({compiled_engine} compiled, {len(engine_objs) - compiled_engine}"
                  f" up to date)")
        else:
            print(f"[build] {self.dll} is up to date ({len(engine_objs)} objects)")

        if not build_app:
            return True

        # ---- app ----
        compiled_app = compile_all(app, is_engine=False)
        if compiled_app is None:
            return False
        app_objs = [self.obj_of(s) for s, _ in app]

        out_abs = self.abspath(self.out)
        dll_abs = self.abspath(self.dll)
        need_link = compiled_app > 0 or not os.path.exists(out_abs)
        if not need_link:
            for o in app_objs:
                if os.path.getmtime(self.abspath(o)) > os.path.getmtime(out_abs):
                    need_link = True
                    break
            if not need_link and os.path.exists(dll_abs) and \
                    os.path.getmtime(dll_abs) > os.path.getmtime(out_abs):
                need_link = True                      # exports may have changed
        if need_link:
            if not self.link_app(app_objs):
                return False
            print(f"[build] built {self.out} "
                  f"({compiled_app} compiled, {len(app_objs) - compiled_app}"
                  f" up to date)")
        else:
            print(f"[build] {self.out} is up to date ({len(app_objs)} objects)")
        if not self.copy_api_to_release():
            return False
        return True

    def build_demo(self, name):
        """builds demos/<name>.c into an executable next to the engine"""
        if not self.cc:
            print("[build] ERROR: no C compiler found")
            return False
        src = f"demos/{name}.c"
        if not os.path.isfile(self.abspath(src)):
            print(f"[build] ERROR: demo source not found: {src}")
            return False

        if not self.build(build_app=False):
            return False

        obj = self.obj_of(src)
        os.makedirs(self.abspath(self.obj_dir), exist_ok=True)
        if self.is_stale(src, obj):
            if not self.compile(src, third_party=False, engine=False):
                return False
        out = os.path.join(self.build_dir,
                           name + (".exe" if self.is_windows else ""))
        if not self.link_app([obj], out):
            return False
        print(f"[build] built {out}")
        return True

    def copy_api_to_release(self):
        source = self.abspath("API")
        target = self.abspath(os.path.join(self.build_dir, "API"))
        if not os.path.isdir(source):
            print(f"[build] ERROR: SDK directory not found: {source}")
            return False

        def raise_walk_error(error):
            raise error

        try:
            for current, _, files in os.walk(source, onerror=raise_walk_error):
                relative = os.path.relpath(current, source)
                destination_dir = target if relative == "." else \
                    os.path.join(target, relative)
                os.makedirs(destination_dir, exist_ok=True)
                for name in files:
                    shutil.copy2(os.path.join(current, name),
                                 os.path.join(destination_dir, name))
        except OSError as exc:
            print(f"[build] ERROR: cannot copy SDK to {target}: {exc}")
            return False

        print(f"[build] copied API/ to {os.path.join(self.build_dir, 'API')}")
        return True

    def export_sdk(self):
        include_dir = os.path.join("API", "include")
        for header in self.sdk_headers:
            source = self.abspath(os.path.join("core", header))
            target = self.abspath(os.path.join(include_dir, header))
            if not os.path.isfile(source):
                print(f"[sdk] ERROR: public header not found: {source}")
                return False
            os.makedirs(os.path.dirname(target), exist_ok=True)
            try:
                shutil.copy2(source, target)
            except OSError as exc:
                print(f"[sdk] ERROR: cannot export {header}: {exc}")
                return False

        umbrella = self.abspath(os.path.join("core", "sdge.h"))
        try:
            shutil.copy2(umbrella, self.abspath(os.path.join(include_dir, "sdge.h")))
        except OSError as exc:
            print(f"[sdk] ERROR: cannot export sdge.h: {exc}")
            return False

        lib_dir = os.path.join("API", "lib", self.platform, self.variant)
        target_dir = self.abspath(lib_dir)
        os.makedirs(target_dir, exist_ok=True)
        artifacts = [(self.dll, os.path.basename(self.dll))]
        if self.implib:
            artifacts.append((self.implib, os.path.basename(self.implib)))
        for source_rel, name in artifacts:
            source = self.abspath(source_rel)
            if not os.path.isfile(source):
                print(f"[sdk] ERROR: build artifact not found: {source}")
                return False
            try:
                shutil.copy2(source, os.path.join(target_dir, name))
            except OSError as exc:
                print(f"[sdk] ERROR: cannot export {name}: {exc}")
                return False

        print(f"[sdk] exported C headers to {include_dir}")
        print(f"[sdk] exported {self.platform}/{self.variant} libraries to {lib_dir}")
        return True

    def sdk(self):
        return self.build(build_app=False) and self.export_sdk()

    def run(self):
        if not self.build():
            return False
        print(f"[run] starting {self.out} ...")
        r = subprocess.run([self.out], cwd=ROOT)
        if r.returncode != 0:
            print(f"[run] app exited with code {r.returncode}")
            return False
        return True

    def clean(self):
        shutil.rmtree(self.abspath("build"), ignore_errors=True)
        print("[build] removed build/")
        return True

# ----------------------------------------------------------------- wsl glue
def run_in_wsl():
    if os.name != "nt":
        print("ERROR: --wsl makes sense only when running from Windows")
        return 1

    r = subprocess.run(
        ["wsl.exe", "-e", "bash", "-lc", f"wslpath -a {shlex.quote(ROOT)}"],
        capture_output=True, text=True, encoding="utf-8", errors="replace")
    linux_root = r.stdout.strip()
    if not linux_root or r.returncode != 0:
        print("[wsl] ERROR: cannot translate project path:", r.stdout, r.stderr)
        return 1
    print(f"[wsl] project at {linux_root}")

    inner = ["python3", "build.py"] + [a for a in sys.argv[1:] if a != "--wsl"]
    cmd = f"cd {shlex.quote(linux_root)} && " + \
          " ".join(shlex.quote(x) for x in inner)
    print("[wsl]", cmd, "\n")
    r = subprocess.run(["wsl.exe", "-e", "bash", "-lc", cmd])
    return r.returncode


# ----------------------------------------------------------------- entry
def main():
    ap = argparse.ArgumentParser(
        description="Silver Diamond Game Engine build script")
    ap.add_argument("command", nargs="?", default="build",
                    choices=["build", "run", "rebuild", "clean", "sdk", "demo"],
                    help="what to do (default: build)")
    ap.add_argument("--debug", action="store_true",
                    help="-O0 -g build (default: -O2 -DNDEBUG)")
    ap.add_argument("--nogl", action="store_true",
                    help="build without OpenGL (-DSDGE_NO_GL)")
    ap.add_argument("--name", metavar="NAME",
                    help="binary name (default: src/config.py 'name' or 'silver_diamond')")
    ap.add_argument("--demo", metavar="NAME",
                    help="demo program to build (source: demos/NAME.c)")
    ap.add_argument("--wsl", action="store_true",
                    help="build inside WSL instead of Windows")
    args = ap.parse_args()

    if args.wsl:
        return run_in_wsl()

    m = Main(args)
    if args.command == "build":
        return 0 if m.build() else 1
    if args.command == "run":
        return 0 if m.run() else 1
    if args.command == "rebuild":
        return 0 if (m.clean() and m.build()) else 1
    if args.command == "clean":
        return 0 if m.clean() else 1
    if args.command == "sdk":
        return 0 if m.sdk() else 1
    if args.command == "demo":
        name = args.demo or "texture_demo"
        return 0 if m.build_demo(name) else 1
    return 1


if __name__ == "__main__":
    sys.exit(main())
