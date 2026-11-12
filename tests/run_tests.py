"""
tests/run_tests.py - build & run the window.h test suite.

Author - ExeBoi / SDGE test harness
Usage:
    python tests/run_tests.py             # build+run for the CURRENT platform
    python tests/run_tests.py --win       # only Windows (MinGW)
    python tests/run_tests.py --wsl       # only WSL/Linux (from Windows)
    python tests/run_tests.py --all       # Windows + WSL (from Windows)

Linux side needs: gcc, libx11-dev, libgl1-mesa-dev, xvfb, x11-utils.
Tests run under Xvfb when available (fully automated, no focus needed),
otherwise they fall back to DISPLAY=:0 (WSLg / existing X server).
"""
import os
import sys
import shlex
import shutil
import argparse
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "tests", "bin")
INC = os.path.join(ROOT, "core", "graphics")

FAIL_SUMMARY = []

# the demo's clear colour (renderer default) - anything else counts as drawn
CLEAR_RGB = (13, 15, 23)


def count_painted(path):
    """counts the pixels of a binary PPM that differ from the clear colour"""
    with open(path, "rb") as f:
        data = f.read()
    i = 0

    def token():
        nonlocal i
        while i < len(data) and data[i:i + 1].isspace():
            i += 1
        if data[i:i + 1] == b"#":
            while i < len(data) and data[i] != 0x0A:
                i += 1
            return token()
        j = i
        while j < len(data) and not data[j:j + 1].isspace():
            j += 1
        t = data[i:j]
        i = j
        return t

    if token() != b"P6":
        return -1, 0, 0
    w = int(token())
    h = int(token())
    token()                      # maxval
    i += 1
    px = data[i:i + w * h * 3]
    if len(px) != w * h * 3:
        return -1, w, h
    painted = 0
    for k in range(0, len(px), 3):
        if (abs(px[k] - CLEAR_RGB[0]) > 3 or abs(px[k + 1] - CLEAR_RGB[1]) > 3
                or abs(px[k + 2] - CLEAR_RGB[2]) > 3):
            painted += 1
    return painted, w, h


def check_demo_frame(exe, cwd, label, xvfb=False):
    """runs the demo in --shot mode and verifies that it drew something"""
    if not os.path.exists(exe):
        print(f"[{label}] demo frame check SKIPPED: {exe} not built "
              "(run: python build.py)")
        return True
    shot = os.path.join(cwd, "tests", "bin", "demo_frame.ppm")
    if xvfb and shutil.which("xvfb-run"):
        cmd = ["xvfb-run", "-a", "-s", "-screen 0 1280x1024x24", exe,
               "--shot", "30", shot]
    else:
        cmd = [exe, "--shot", "30", shot]
    print(f"[{label}] demo frame check: {' '.join(shlex.quote(c) for c in cmd)}")
    try:
        r = subprocess.run(cmd, capture_output=True, text=True,
                           encoding="utf-8", errors="replace", cwd=cwd,
                           timeout=300)
    except subprocess.TimeoutExpired:
        print(f"[{label}] demo frame check FAILED: timeout")
        FAIL_SUMMARY.append(f"{label}/demo-frame: timeout")
        return False
    if not os.path.exists(shot):
        print(f"[{label}] demo frame check FAILED: no screenshot produced")
        FAIL_SUMMARY.append(f"{label}/demo-frame: no screenshot")
        return False
    painted, w, h = count_painted(shot)
    print(f"[{label}] demo frame: {w}x{h}, painted pixels: {painted}")
    if painted < 1000:
        print(f"[{label}] demo frame check FAILED: the frame is empty")
        FAIL_SUMMARY.append(f"{label}/demo-frame: empty frame")
        return False
    print(f"[{label}] demo frame check PASSED")
    return True


# ---------------------------------------------------------------- windows
def find_windows_gcc():
    for cand in (r"C:\MinGW\bin\gcc.exe", "gcc", "clang"):
        p = shutil.which(cand)
        if p:
            return p
    return None


# engine sources needed by the asset/audio test
ASSET_TEST_SRC = [
    "tests/test_assets.c",
    "core/assets/assets.c",
    "core/assets/gltf.c",
    "core/assets/mp3.c",
    "core/audio/audio.c",
    "include/minimp3/minimp3.c",
]
RENDER_TEST_SRC = [
    "tests/test_render.c",
    "core/graphics/window.c",
    "core/graphics/mesh.c",
    "core/graphics/renderer.c",
    "core/assets/assets.c",
    "core/assets/gltf.c",
    "include/glad/glad.c",
]
GUI_TEST_SRC = [
    "tests/test_gui.c",
    "core/graphics/ui.c",
    "core/scene/scene.c",
    "core/subjects/2D/Camera.c",
    "core/subjects/2D/gui/gui.c",
    "core/subjects/2D/gui/div.c",
    "core/subjects/2D/gui/button.c",
    "core/subjects/2D/gui/input.c",
    "core/subjects/2D/gui/label.c",
    "core/subjects/2D/gui/checkbox.c",
    "core/subjects/2D/gui/progress.c",
    "core/subjects/2D/gui/slider.c",
    "core/subjects/2D/gui/radio.c",
    "core/subjects/2D/gui/listbox.c",
    "core/assets/font.c",
    "core/assets/font_cff.c",
    "core/assets/assets.c",
    "include/glad/glad.c",
]
PHYSICS_TEST_SRC = [
    "tests/test_physics.c",
    "core/physics/physics2d.c",
    "core/physics/physics3d.c",
]
FONT_TEST_SRC = [
    "tests/test_font.c",
    "core/assets/font.c",
    "core/assets/font_cff.c",
    "core/assets/assets.c",
]
MP3_ASSET = "assets/Detective(2).mp3"


def build_windows():
    gcc = find_windows_gcc()
    if not gcc:
        print("[windows] ERROR: no gcc/clang found (looked in C:\\MinGW\\bin, PATH)")
        return False
    print(f"[windows] compiler: {gcc}")
    os.makedirs(BIN, exist_ok=True)
    targets = [
        ("test_window.exe",      ["tests/test_window.c"],  [],
         ["-lgdi32", "-lopengl32", "-luser32"]),
        ("test_window_nogl.exe", ["tests/test_window.c"],  ["-DSDGE_NO_GL"],
         ["-lgdi32", "-luser32"]),
        ("demo.exe",             ["tests/demo.c"],         [],
         ["-lgdi32", "-lopengl32", "-luser32"]),
        ("test_assets.exe",      ASSET_TEST_SRC,           [],
         ["-lwinmm", "-lm"]),
        ("test_render.exe",      RENDER_TEST_SRC,          [],
         ["-lgdi32", "-lopengl32", "-luser32", "-lm"]),
        ("test_gui.exe",         GUI_TEST_SRC,             [],
         ["-lgdi32", "-lopengl32", "-luser32", "-lm"]),
        ("test_physics.exe",     PHYSICS_TEST_SRC,         [], ["-lm"]),
        ("test_font.exe",        FONT_TEST_SRC,            [], ["-lm"]),
    ]
    ok = True
    for name, srcs, extra, libs in targets:
        out = os.path.join(BIN, name)
        cmd = [gcc, "-std=c99", "-Wall", "-Wextra", *extra,
               "-I", INC, "-I", os.path.join(ROOT, "core"),
               "-I", os.path.join(ROOT, "include"), *srcs, "-o", out, *libs]
        print("[windows] build:", " ".join(shlex.quote(c) for c in cmd))
        r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8",
                           errors="replace", cwd=ROOT)
        if r.returncode != 0:
            print(r.stdout)
            print(r.stderr)
            print(f"[windows] BUILD FAILED: {name}")
            ok = False
        else:
            if r.stderr.strip():
                print(r.stderr.strip())
            print(f"[windows] built {name}")
    return ok


def run_windows():
    ok = True
    for name in ("test_window.exe", "test_window_nogl.exe"):
        exe = os.path.join(BIN, name)
        if not os.path.exists(exe):
            continue
        print(f"\n[windows] running {name} ...")
        r = subprocess.run([exe], capture_output=True, text=True,
                           encoding="utf-8", errors="replace", timeout=120,
                           cwd=ROOT)
        print(r.stdout)
        if r.returncode != 0:
            print(f"[windows] {name} FAILED (exit code {r.returncode})")
            FAIL_SUMMARY.append(f"windows/{name}: exit {r.returncode}")
            ok = False
        else:
            print(f"[windows] {name} PASSED")

    # asset/audio suite (needs the project root as CWD for its data files)
    exe = os.path.join(BIN, "test_assets.exe")
    if os.path.exists(exe):
        print("\n[windows] running test_assets.exe ...")
        r = subprocess.run([exe, MP3_ASSET], capture_output=True, text=True,
                           encoding="utf-8", errors="replace", timeout=300,
                           cwd=ROOT)
        print(r.stdout)
        if r.returncode != 0:
            print(f"[windows] test_assets.exe FAILED (exit code {r.returncode})")
            FAIL_SUMMARY.append(f"windows/test_assets: exit {r.returncode}")
            ok = False
        else:
            print("[windows] test_assets.exe PASSED")

    # gui / scene suite (headless - no GL context needed)
    exe = os.path.join(BIN, "test_gui.exe")
    if os.path.exists(exe):
        print("\n[windows] running test_gui.exe ...")
        r = subprocess.run([exe], capture_output=True, text=True,
                           encoding="utf-8", errors="replace", timeout=300,
                           cwd=ROOT)
        print(r.stdout)
        if r.returncode != 0:
            print(f"[windows] test_gui.exe FAILED (exit code {r.returncode})")
            FAIL_SUMMARY.append(f"windows/test_gui: exit {r.returncode}")
            ok = False
        else:
            print("[windows] test_gui.exe PASSED")

    # physics and font suites (headless)
    for name, args in (("test_physics.exe", []), ("test_font.exe", [])):
        exe = os.path.join(BIN, name)
        if not os.path.exists(exe):
            continue
        print(f"\n[windows] running {name} ...")
        r = subprocess.run([exe, *args], capture_output=True, text=True,
                           encoding="utf-8", errors="replace", timeout=300,
                           cwd=ROOT)
        print(r.stdout)
        if r.returncode != 0:
            print(f"[windows] {name} FAILED (exit code {r.returncode})")
            FAIL_SUMMARY.append(f"windows/{name}: exit {r.returncode}")
            ok = False
        else:
            print(f"[windows] {name} PASSED")

    # renderer suite (needs a GL context)
    exe = os.path.join(BIN, "test_render.exe")
    if os.path.exists(exe):
        print("\n[windows] running test_render.exe ...")
        r = subprocess.run([exe], capture_output=True, text=True,
                           encoding="utf-8", errors="replace", timeout=300,
                           cwd=ROOT)
        print(r.stdout)
        if r.returncode != 0:
            print(f"[windows] test_render.exe FAILED (exit code {r.returncode})")
            FAIL_SUMMARY.append(f"windows/test_render: exit {r.returncode}")
            ok = False
        else:
            print("[windows] test_render.exe PASSED")

    ok = check_demo_frame(os.path.join(ROOT, "build", "windows", "release",
                                       "silver_diamond.exe"),
                          ROOT, "windows") and ok

    print("\n[windows] demo.exe is interactive - run it manually to check visuals")
    return ok


# ---------------------------------------------------------------- linux
def build_linux():
    print("[linux] compiler: gcc")
    os.makedirs(BIN, exist_ok=True)
    alsa = (["-lasound"] if os.path.exists("/usr/include/alsa/asoundlib.h")
            else ["-DSDGE_NO_ALSA"])
    targets = [
        ("test_window_linux",      ["tests/test_window.c"], [],
         ["-lX11", "-lGL", "-lm"]),
        ("test_window_nogl_linux", ["tests/test_window.c"], ["-DSDGE_NO_GL"],
         ["-lX11"]),
        ("demo_linux",             ["tests/demo.c"],        [],
         ["-lX11", "-lGL", "-lm"]),
        ("test_assets_linux",      ASSET_TEST_SRC,          [], alsa + ["-lm"]),
        ("test_render_linux",      RENDER_TEST_SRC,         [], ["-lX11", "-lGL", "-lm"]),
        ("test_gui_linux",         GUI_TEST_SRC,            [], ["-lX11", "-lGL", "-lm"]),
        ("test_physics_linux",     PHYSICS_TEST_SRC,        [], ["-lm"]),
        ("test_font_linux",        FONT_TEST_SRC,           [], ["-lm"]),
    ]
    ok = True
    for name, srcs, extra, libs in targets:
        cmd = ["gcc", "-std=gnu99", "-Wall", "-Wextra", *extra,
               "-I", "core/graphics", "-I", "core", "-I", "include",
               *srcs, "-o", f"tests/bin/{name}", *libs]
        print("[linux] build:", " ".join(shlex.quote(c) for c in cmd))
        r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8",
                           errors="replace", cwd=ROOT)
        if r.returncode != 0:
            print(r.stdout)
            print(r.stderr)
            print(f"[linux] BUILD FAILED: {name}")
            ok = False
        else:
            if r.stderr.strip():
                print(r.stderr.strip())
            print(f"[linux] built {name}")
    return ok


def run_linux():
    ok = True
    for name in ("test_window_linux", "test_window_nogl_linux"):
        exe = f"tests/bin/{name}"
        if not os.path.exists(os.path.join(ROOT, exe)):
            continue
        if shutil.which("xvfb-run"):
            cmd = ["xvfb-run", "-a", "-s", "-screen 0 1280x1024x24", exe]
        elif os.path.exists("/tmp/.X11-unix/X0"):
            cmd = [f"DISPLAY=:0 {exe}"]
        else:
            print(f"[linux] {name} SKIPPED: no Xvfb and no X server on :0")
            continue
        print(f"\n[linux] running {name} ({cmd[0]} setup) ...")
        if isinstance(cmd[0], str) and " " in cmd[0]:      # shell-style fallback
            r = subprocess.run(cmd[0], shell=True, capture_output=True,
                               text=True, encoding="utf-8", errors="replace",
                               cwd=ROOT, timeout=120)
        else:
            r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8",
                               errors="replace", cwd=ROOT, timeout=120)
        print(r.stdout)
        if r.returncode != 0:
            print(f"[linux] {name} FAILED (exit code {r.returncode})")
            FAIL_SUMMARY.append(f"linux/{name}: exit {r.returncode}")
            ok = False
        else:
            print(f"[linux] {name} PASSED")

    # asset/audio suite (headless: no X server needed)
    exe = "tests/bin/test_assets_linux"
    if os.path.exists(os.path.join(ROOT, exe)):
        print("\n[linux] running test_assets_linux ...")
        r = subprocess.run([exe, MP3_ASSET], capture_output=True, text=True,
                           encoding="utf-8", errors="replace", cwd=ROOT,
                           timeout=300)
        print(r.stdout)
        if r.returncode != 0:
            print(f"[linux] test_assets_linux FAILED (exit code {r.returncode})")
            FAIL_SUMMARY.append(f"linux/test_assets: exit {r.returncode}")
            ok = False
        else:
            print("[linux] test_assets_linux PASSED")

    # gui / scene suite (headless - no GL context needed)
    exe = "tests/bin/test_gui_linux"
    if os.path.exists(os.path.join(ROOT, exe)):
        print("\n[linux] running test_gui_linux ...")
        r = subprocess.run([exe], capture_output=True, text=True,
                           encoding="utf-8", errors="replace", cwd=ROOT,
                           timeout=300)
        print(r.stdout)
        if r.returncode != 0:
            print(f"[linux] test_gui_linux FAILED (exit code {r.returncode})")
            FAIL_SUMMARY.append(f"linux/test_gui: exit {r.returncode}")
            ok = False
        else:
            print("[linux] test_gui_linux PASSED")

    # physics and font suites (headless)
    for name in ("test_physics_linux", "test_font_linux"):
        exe = os.path.join("tests", "bin", name)
        if not os.path.exists(os.path.join(ROOT, exe)):
            continue
        print(f"\n[linux] running {name} ...")
        r = subprocess.run([exe], capture_output=True, text=True,
                           encoding="utf-8", errors="replace", cwd=ROOT,
                           timeout=300)
        print(r.stdout)
        if r.returncode != 0:
            print(f"[linux] {name} FAILED (exit code {r.returncode})")
            FAIL_SUMMARY.append(f"linux/{name}: exit {r.returncode}")
            ok = False
        else:
            print(f"[linux] {name} PASSED")

    # renderer suite (needs a GL context -> Xvfb when available)
    exe = "tests/bin/test_render_linux"
    if os.path.exists(os.path.join(ROOT, exe)):
        print("\n[linux] running test_render_linux ...")
        if shutil.which("xvfb-run"):
            cmd = ["xvfb-run", "-a", "-s", "-screen 0 1280x1024x24", exe]
            r = subprocess.run(cmd, capture_output=True, text=True,
                               encoding="utf-8", errors="replace", cwd=ROOT,
                               timeout=300)
        elif os.path.exists("/tmp/.X11-unix/X0"):
            r = subprocess.run([f"DISPLAY=:0 {exe}"], shell=True,
                               capture_output=True, text=True,
                               encoding="utf-8", errors="replace", cwd=ROOT,
                               timeout=300)
        else:
            print("[linux] test_render_linux SKIPPED: no Xvfb and no X server on :0")
            r = None
        if r is not None:
            print(r.stdout)
            if r.returncode != 0:
                print(f"[linux] test_render_linux FAILED (exit code {r.returncode})")
                FAIL_SUMMARY.append(f"linux/test_render: exit {r.returncode}")
                ok = False
            else:
                print("[linux] test_render_linux PASSED")

    ok = check_demo_frame(os.path.join(ROOT, "build", "linux", "release",
                                       "silver_diamond"),
                          ROOT, "linux", xvfb=True) and ok

    print("\n[linux] demo_linux is interactive - run it manually to check visuals")
    return ok


# ---------------------------------------------------------------- wsl glue
def wsl_run(args_list, cwd=None, timeout=300):
    cmd = " ".join(shlex.quote(a) for a in args_list)
    if cwd:
        cmd = f"cd {shlex.quote(cwd)} && {cmd}"
    return subprocess.run(["wsl.exe", "-e", "bash", "-lc", cmd],
                          capture_output=True, text=True, encoding="utf-8",
                          errors="replace", timeout=timeout)


def run_in_wsl():
    # translate the project path into the WSL filesystem
    r = wsl_run(["wslpath", "-a", ROOT])
    linux_root = r.stdout.strip()
    if not linux_root or r.returncode != 0:
        print("[wsl] ERROR: cannot translate project path:", r.stdout, r.stderr)
        return False
    print(f"[wsl] project at {linux_root}")

    # check the toolchain
    r = wsl_run(["bash", "-c",
                 "command -v gcc && command -v xvfb-run; "
                 "test -f /usr/include/X11/Xlib.h && echo X11-OK; "
                 "test -f /usr/include/GL/glx.h && echo GLX-OK"])
    out = (r.stdout or "") + (r.stderr or "")
    print("[wsl] toolchain:", out.replace("\n", " ").strip())
    if "X11-OK" not in out:
        print("[wsl] ERROR: X11 dev headers missing "
              "(sudo apt-get install libx11-dev libgl1-mesa-dev xvfb)")
        return False

    # reuse this script inside WSL
    script_rel = os.path.relpath(os.path.abspath(__file__), ROOT).replace("\\", "/")
    r = wsl_run(["python3", script_rel], cwd=linux_root, timeout=600)
    print(r.stdout)
    ok = (r.returncode == 0)
    if not ok:
        print(r.stderr)
        FAIL_SUMMARY.append(f"wsl run: exit {r.returncode}")
    return ok


# ---------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--win", action="store_true", help="run Windows tests only")
    ap.add_argument("--wsl", action="store_true", help="run WSL/Linux tests only")
    ap.add_argument("--all", action="store_true", help="run both (from Windows)")
    args = ap.parse_args()

    on_windows = os.name == "nt"
    if args.all:
        do_win, do_wsl = on_windows, True
    elif args.win:
        do_win, do_wsl = True, False
    elif args.wsl:
        do_win, do_wsl = False, True
    else:
        do_win, do_wsl = on_windows, not on_windows

    results = []

    if do_win:
        print("======== Windows (MinGW) ========")
        ok = build_windows() and run_windows()
        results.append(("windows", ok))

    if do_wsl:
        if on_windows:
            print("\n======== WSL (Ubuntu / Xvfb) ========")
            ok = run_in_wsl()
            results.append(("wsl", ok))
        else:
            print("======== Linux (native or WSL) ========")
            ok = build_linux() and run_linux()
            results.append(("linux", ok))

    print("\n================ SUMMARY ================")
    for name, ok in results:
        print(f"  {name:8s}: {'PASSED' if ok else 'FAILED'}")
    if FAIL_SUMMARY:
        print("  details:", *FAIL_SUMMARY, sep="\n    ")
    print("=========================================")
    return 0 if all(ok for _, ok in results) else 1


if __name__ == "__main__":
    sys.exit(main())
