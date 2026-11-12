#!/usr/bin/env python3
"""make_assets.py - Silver Diamond Game Engine :: asset generator

Generates (deterministically, stdlib only):
  Subjects/cube.glb, sphere.glb, plane.glb, cylinder.glb, cone.glb,
  torus.glb, character.glb (rigged, 2 animations), beep.wav
  tests/data/triangle.gltf + triangle.bin   (external-buffer glTF)
  tests/data/beep.wav                       (44.1 kHz -> exercises resampler)

The generated character is a full skinning test: 17-bone skeleton, one
skinned mesh (boxes weighted per bone), inverse bind matrices, and two
animations ("idle" with a known hips-height curve, "wave").
Run:  python tools/make_assets.py
"""
import json
import math
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# glTF component types
BYTE, UNSIGNED_BYTE = 5120, 5121
SHORT, UNSIGNED_SHORT = 5122, 5123
UNSIGNED_INT, FLOAT = 5125, 5126

COMP_SIZE = {BYTE: 1, UNSIGNED_BYTE: 1, SHORT: 2, UNSIGNED_SHORT: 2,
             UNSIGNED_INT: 4, FLOAT: 4}
TYPE_COMPS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


# ------------------------------------------------------------- small math
def mat_mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)]
            for i in range(4)]


def mat_translate(x, y, z):
    return [[1, 0, 0, x], [0, 1, 0, y], [0, 0, 1, z], [0, 0, 0, 1]]


def mat_invert(m):
    """general 4x4 inverse (Cramer); identity on singular"""
    a = m
    b00 = a[0][0]*a[1][1] - a[0][1]*a[1][0]
    b01 = a[0][0]*a[1][2] - a[0][2]*a[1][0]
    b02 = a[0][0]*a[1][3] - a[0][3]*a[1][0]
    b03 = a[0][1]*a[1][2] - a[0][2]*a[1][1]
    b04 = a[0][1]*a[1][3] - a[0][3]*a[1][1]
    b05 = a[0][2]*a[1][3] - a[0][3]*a[1][2]
    b06 = a[2][0]*a[3][1] - a[2][1]*a[3][0]
    b07 = a[2][0]*a[3][2] - a[2][2]*a[3][0]
    b08 = a[2][0]*a[3][3] - a[2][3]*a[3][0]
    b09 = a[2][1]*a[3][2] - a[2][2]*a[3][1]
    b10 = a[2][1]*a[3][3] - a[2][3]*a[3][1]
    b11 = a[2][2]*a[3][3] - a[2][3]*a[3][2]
    det = b00*b11 - b01*b10 + b02*b09 + b03*b08 - b04*b07 + b05*b06
    if abs(det) < 1e-12:
        return [[1 if i == j else 0 for j in range(4)] for i in range(4)]
    inv = 1.0 / det
    return [
        [(a[1][1]*b11 - a[1][2]*b10 + a[1][3]*b09) * inv,
         (a[0][2]*b10 - a[0][1]*b11 - a[0][3]*b09) * inv,
         (a[3][1]*b05 - a[3][2]*b04 + a[3][3]*b03) * inv,
         (a[2][2]*b04 - a[2][1]*b05 - a[2][3]*b03) * inv],
        [(a[1][2]*b08 - a[1][0]*b11 - a[1][3]*b07) * inv,
         (a[0][0]*b11 - a[0][2]*b08 + a[0][3]*b07) * inv,
         (a[3][2]*b02 - a[3][0]*b05 - a[3][3]*b01) * inv,
         (a[2][0]*b05 - a[2][2]*b02 + a[2][3]*b01) * inv],
        [(a[1][0]*b10 - a[1][1]*b08 + a[1][3]*b06) * inv,
         (a[0][1]*b08 - a[0][0]*b10 - a[0][3]*b06) * inv,
         (a[3][0]*b04 - a[3][1]*b02 + a[3][3]*b00) * inv,
         (a[2][1]*b02 - a[2][0]*b04 - a[2][3]*b00) * inv],
        [(a[1][1]*b07 - a[1][0]*b09 - a[1][2]*b06) * inv,
         (a[0][0]*b09 - a[0][1]*b07 + a[0][2]*b06) * inv,
         (a[3][1]*b01 - a[3][0]*b03 - a[3][2]*b00) * inv,
         (a[2][0]*b03 - a[2][1]*b01 + a[2][2]*b00) * inv],
    ]


def quat_axis_angle(ax, ay, az, deg):
    rad = math.radians(deg)
    h = rad * 0.5
    s = math.sin(h)
    return [ax * s, ay * s, az * s, math.cos(h)]


# ------------------------------------------------------------ GLB builder
class Gltf:
    def __init__(self):
        self.bin = bytearray()
        self.views = []
        self.accessors = []

    def view(self, data):
        while len(self.bin) % 4:
            self.bin += b"\0"
        off = len(self.bin)
        self.bin += data
        self.views.append({"buffer": 0, "byteOffset": off,
                           "byteLength": len(data)})
        return len(self.views) - 1

    def acc(self, data, ctype, atype, count, mn=None, mx=None, normalized=False):
        v = self.view(data)
        a = {"bufferView": v, "componentType": ctype, "count": count,
             "type": atype}
        if normalized:
            a["normalized"] = True
        if mn is not None:
            a["min"] = [round(x, 6) for x in mn]
            a["max"] = [round(x, 6) for x in mx]
        self.accessors.append(a)
        return len(self.accessors) - 1

    def acc_f32(self, values, atype, mn=None, mx=None):
        comps = TYPE_COMPS[atype]
        flat = []
        for v in values:
            flat.extend(v if isinstance(v, (list, tuple)) else [v])
        data = struct.pack("<%df" % len(flat), *flat)
        return self.acc(data, FLOAT, atype, len(flat) // comps, mn, mx)

    def acc_u16(self, values):
        data = struct.pack("<%dH" % len(values), *values)
        return self.acc(data, UNSIGNED_SHORT, "SCALAR", len(values))

    def acc_u32(self, values):
        data = struct.pack("<%dI" % len(values), *values)
        return self.acc(data, UNSIGNED_INT, "SCALAR", len(values))

    def acc_u8(self, values, atype):
        comps = TYPE_COMPS[atype]
        data = bytes(values)
        return self.acc(data, UNSIGNED_BYTE, atype, len(values) // comps)

    def build(self, nodes, meshes, skins=None, animations=None, materials=None,
              scene_roots=None):
        doc = {
            "asset": {"version": "2.0", "generator": "sdge make_assets.py"},
            "scene": 0,
            "scenes": [{"nodes": scene_roots if scene_roots else [0]}],
            "nodes": nodes,
            "meshes": meshes,
            "buffers": [{"byteLength": len(self.bin)}],
        }
        if self.accessors:
            doc["accessors"] = self.accessors
        if self.views:
            doc["bufferViews"] = self.views
        if skins:
            doc["skins"] = skins
        if animations:
            doc["animations"] = animations
        if materials:
            doc["materials"] = materials

        js = json.dumps(doc, separators=(",", ":")).encode("utf-8")
        while len(js) % 4:
            js += b" "
        while len(self.bin) % 4:
            self.bin += b"\0"
        total = 12 + 8 + len(js) + 8 + len(self.bin)
        out = struct.pack("<III", 0x46546C67, 2, total)
        out += struct.pack("<II", len(js), 0x4E4F534A) + js
        out += struct.pack("<II", len(self.bin), 0x004E4942) + bytes(self.bin)
        return out


# ------------------------------------------------------------- geometry
def box(cx, cy, cz, sx, sy, sz):
    """axis-aligned box, 24 verts (flat normals)"""
    hx, hy, hz = sx * 0.5, sy * 0.5, sz * 0.5
    faces = [
        ([(cx - hx, cy - hy, cz + hz), (cx + hx, cy - hy, cz + hz),
          (cx + hx, cy + hy, cz + hz), (cx - hx, cy + hy, cz + hz)], (0, 0, 1)),
        ([(cx + hx, cy - hy, cz - hz), (cx - hx, cy - hy, cz - hz),
          (cx - hx, cy + hy, cz - hz), (cx + hx, cy + hy, cz - hz)], (0, 0, -1)),
        ([(cx + hx, cy - hy, cz + hz), (cx + hx, cy - hy, cz - hz),
          (cx + hx, cy + hy, cz - hz), (cx + hx, cy + hy, cz + hz)], (1, 0, 0)),
        ([(cx - hx, cy - hy, cz - hz), (cx - hx, cy - hy, cz + hz),
          (cx - hx, cy + hy, cz + hz), (cx - hx, cy + hy, cz - hz)], (-1, 0, 0)),
        ([(cx - hx, cy + hy, cz + hz), (cx + hx, cy + hy, cz + hz),
          (cx + hx, cy + hy, cz - hz), (cx - hx, cy + hy, cz - hz)], (0, 1, 0)),
        ([(cx - hx, cy - hy, cz - hz), (cx + hx, cy - hy, cz - hz),
          (cx + hx, cy - hy, cz + hz), (cx - hx, cy - hy, cz + hz)], (0, -1, 0)),
    ]
    pos, nrm, idx = [], [], []
    for verts, n in faces:
        base = len(pos)
        for v in verts:
            pos.append(v)
            nrm.append(n)
        idx += [base, base + 1, base + 2, base, base + 2, base + 3]
    return pos, nrm, idx


def sphere(radius, seg=24, rings=16):
    pos, nrm, idx = [], [], []
    for r in range(rings + 1):
        phi = math.pi * r / rings
        for s in range(seg + 1):
            th = 2 * math.pi * s / seg
            x = math.sin(phi) * math.cos(th)
            y = math.cos(phi)
            z = math.sin(phi) * math.sin(th)
            pos.append((x * radius, y * radius, z * radius))
            nrm.append((x, y, z))
    for r in range(rings):
        for s in range(seg):
            a = r * (seg + 1) + s
            b = a + seg + 1
            idx += [a, b, a + 1, a + 1, b, b + 1]
    return pos, nrm, idx


def plane(size=2.0, subdiv=1):
    pos, nrm, idx = [], [], []
    n = subdiv + 1
    for j in range(n):
        for i in range(n):
            x = (i / subdiv - 0.5) * size
            z = (j / subdiv - 0.5) * size
            pos.append((x, 0.0, z))
            nrm.append((0.0, 1.0, 0.0))
    for j in range(subdiv):
        for i in range(subdiv):
            a = j * n + i
            b = a + n
            idx += [a, b, a + 1, a + 1, b, b + 1]
    return pos, nrm, idx


def cylinder(radius, height, seg=24):
    pos, nrm, idx = [], [], []
    hy = height * 0.5
    for j in (0, 1):
        y = -hy + j * height
        for s in range(seg + 1):
            th = 2 * math.pi * s / seg
            x, z = math.cos(th), math.sin(th)
            pos.append((x * radius, y, z * radius))
            nrm.append((x, 0.0, z))
    for s in range(seg):
        a, b = s, s + seg + 1
        idx += [a, b, a + 1, a + 1, b, b + 1]
    # caps
    base = len(pos)
    pos.append((0, hy, 0)); nrm.append((0, 1, 0))
    for s in range(seg):
        th = 2 * math.pi * s / seg
        pos.append((math.cos(th) * radius, hy, math.sin(th) * radius))
        nrm.append((0, 1, 0))
    for s in range(seg):
        idx += [base, base + 1 + s, base + 1 + (s + 1) % seg]
    base = len(pos)
    pos.append((0, -hy, 0)); nrm.append((0, -1, 0))
    for s in range(seg):
        th = 2 * math.pi * s / seg
        pos.append((math.cos(th) * radius, -hy, math.sin(th) * radius))
        nrm.append((0, -1, 0))
    for s in range(seg):
        idx += [base, base + 1 + (s + 1) % seg, base + 1 + s]
    return pos, nrm, idx


def cone(radius, height, seg=24):
    pos, nrm, idx = [], [], []
    hy = height * 0.5
    slope = 1.0 / math.sqrt(1 + (height / radius) ** 2)
    for j, y in enumerate((-hy, hy)):
        for s in range(seg + 1):
            th = 2 * math.pi * s / seg
            x, z = math.cos(th), math.sin(th)
            pos.append((x * radius * (1 - j), y, z * radius * (1 - j)))
            nrm.append((x * slope, (height / radius) * slope, z * slope))
    for s in range(seg):
        a, b = s, s + seg + 1
        idx += [a, b, a + 1, a + 1, b, b + 1]
    base = len(pos)
    pos.append((0, -hy, 0)); nrm.append((0, -1, 0))
    for s in range(seg):
        th = 2 * math.pi * s / seg
        pos.append((math.cos(th) * radius, -hy, math.sin(th) * radius))
        nrm.append((0, -1, 0))
    for s in range(seg):
        idx += [base, base + 1 + (s + 1) % seg, base + 1 + s]
    return pos, nrm, idx


def torus(R, r, seg=32, ring=16):
    pos, nrm, idx = [], [], []
    for i in range(seg + 1):
        u = 2 * math.pi * i / seg
        cu, su = math.cos(u), math.sin(u)
        for j in range(ring + 1):
            v = 2 * math.pi * j / ring
            cv, sv = math.cos(v), math.sin(v)
            x = (R + r * cv) * cu
            y = r * sv
            z = (R + r * cv) * su
            pos.append((x, y, z))
            nrm.append((cv * cu, sv, cv * su))
    n = ring + 1
    for i in range(seg):
        for j in range(ring):
            a = i * n + j
            b = (i + 1) * n + j
            idx += [a, b, a + 1, a + 1, b, b + 1]
    return pos, nrm, idx


def bounds(pos):
    mn = [min(p[i] for p in pos) for i in range(3)]
    mx = [max(p[i] for p in pos) for i in range(3)]
    return mn, mx


def simple_glb(geom, name, color, metallic=0.0, roughness=0.55):
    pos, nrm, idx = geom
    g = Gltf()
    a_pos = g.acc_f32(pos, "VEC3", *bounds(pos))
    a_nrm = g.acc_f32(nrm, "VEC3")
    a_idx = g.acc_u16(idx) if len(pos) < 65536 else g.acc_u32(idx)
    mesh = {"name": name,
            "primitives": [{"attributes": {"POSITION": a_pos, "NORMAL": a_nrm},
                            "indices": a_idx, "material": 0, "mode": 4}]}
    nodes = [{"mesh": 0, "name": name}]
    mat = {"name": name + "_mat",
           "pbrMetallicRoughness": {"baseColorFactor": [*color, 1.0],
                                     "metallicFactor": metallic,
                                     "roughnessFactor": roughness}}
    return g.build(nodes, [mesh], materials=[mat])


# --------------------------------------------------------- rigged model
def character_glb():
    g = Gltf()

    # skeleton: node index -> (parent, translation)
    bones = {
        0:  (-1, (0.00, 0.95, 0.00), "Hips"),
        1:  (0,  (0.00, 0.10, 0.00), "Spine"),
        2:  (1,  (0.00, 0.12, 0.00), "Chest"),
        3:  (2,  (0.00, 0.18, 0.00), "Head"),
        4:  (2,  (0.09, 0.10, 0.00), "L_Shoulder"),
        5:  (4,  (0.00, -0.26, 0.00), "L_Elbow"),
        6:  (5,  (0.00, -0.24, 0.00), "L_Hand"),
        7:  (2,  (-0.09, 0.10, 0.00), "R_Shoulder"),
        8:  (7,  (0.00, -0.26, 0.00), "R_Elbow"),
        9:  (8,  (0.00, -0.24, 0.00), "R_Hand"),
        10: (0,  (0.05, -0.05, 0.00), "L_Thigh"),
        11: (10, (0.00, -0.42, 0.00), "L_Shin"),
        12: (11, (0.00, -0.40, 0.00), "L_Foot"),
        13: (0,  (-0.05, -0.05, 0.00), "R_Thigh"),
        14: (13, (0.00, -0.42, 0.00), "R_Shin"),
        15: (14, (0.00, -0.40, 0.00), "R_Foot"),
        16: (3,  (0.00,  0.16, 0.00), "Hat"),
    }

    # body parts: (bone, box center in bone space, box size)
    parts = [
        (0,  (0, -0.02, 0), (0.18, 0.14, 0.11)),    # pelvis
        (2,  (0, 0.02, 0),  (0.21, 0.26, 0.12)),    # torso
        (3,  (0, 0.08, 0),  (0.13, 0.15, 0.13)),    # head
        (4,  (0, -0.13, 0), (0.05, 0.28, 0.05)),    # L upper arm
        (5,  (0, -0.12, 0), (0.045, 0.26, 0.045)),  # L forearm
        (6,  (0, -0.10, 0), (0.05, 0.20, 0.03)),    # L hand
        (7,  (0, -0.13, 0), (0.05, 0.28, 0.05)),    # R upper arm
        (8,  (0, -0.12, 0), (0.045, 0.26, 0.045)),  # R forearm
        (9,  (0, -0.10, 0), (0.05, 0.20, 0.03)),    # R hand
        (10, (0, -0.21, 0), (0.07, 0.44, 0.07)),    # L thigh
        (11, (0, -0.20, 0), (0.06, 0.42, 0.06)),    # L shin
        (12, (0, -0.03, 0.05), (0.07, 0.05, 0.16)), # L foot
        (13, (0, -0.21, 0), (0.07, 0.44, 0.07)),    # R thigh
        (14, (0, -0.20, 0), (0.06, 0.42, 0.06)),    # R shin
        (15, (0, -0.03, 0.05), (0.07, 0.05, 0.16)), # R foot
        (16, (0, 0.09, 0),  (0.17, 0.07, 0.17)),    # hat
    ]

    pos, nrm, idx, joints, weights = [], [], [], [], []
    for bone, c, s in parts:
        p, n, i = box(*c, *s)
        base = len(pos)
        pos.extend(p)
        nrm.extend(n)
        idx.extend([k + base for k in i])
        for _ in p:
            joints.append((bone, 0, 0, 0))
            weights.append((1.0, 0.0, 0.0, 0.0))

    a_pos = g.acc_f32(pos, "VEC3", *bounds(pos))
    a_nrm = g.acc_f32(nrm, "VEC3")
    a_idx = g.acc_u16(idx)
    jflat, wflat = [], []
    for j, w in zip(joints, weights):
        jflat.extend(j)
        wflat.extend(w)
    a_joints = g.acc_u8(jflat, "VEC4")
    a_weights = g.acc_f32(wflat, "VEC4")

    mesh = {"name": "character",
            "primitives": [{"attributes": {"POSITION": a_pos, "NORMAL": a_nrm,
                                            "JOINTS_0": a_joints,
                                            "WEIGHTS_0": a_weights},
                            "indices": a_idx, "material": 0, "mode": 4}]}

    # nodes: children lists from parents
    children = {i: [] for i in bones}
    for i, (parent, _, _) in bones.items():
        if parent >= 0:
            children[parent].append(i)
    nodes = []
    for i in sorted(bones):
        parent, t, name = bones[i]
        n = {"name": name, "translation": list(t)}
        if i == 0:
            n["mesh"] = 0
            n["skin"] = 0
        if children[i]:
            n["children"] = children[i]
        nodes.append(n)

    # inverse bind matrices: bind pose is translation-only, so globals are
    # pure translations
    global_t = {}
    for i in sorted(bones):
        parent, t, _ = bones[i]
        base = global_t.get(parent, (0.0, 0.0, 0.0)) if parent >= 0 else (0.0, 0.0, 0.0)
        global_t[i] = (base[0] + t[0], base[1] + t[1], base[2] + t[2])
    ibm = []
    for i in sorted(bones):
        x, y, z = global_t[i]
        m = mat_invert(mat_translate(x, y, z))
        ibm.append([m[0][0], m[1][0], m[2][0], m[3][0],
                    m[0][1], m[1][1], m[2][1], m[3][1],
                    m[0][2], m[1][2], m[2][2], m[3][2],
                    m[0][3], m[1][3], m[2][3], m[3][3]])
    a_ibm = g.acc_f32(ibm, "MAT4")

    skin = {"joints": sorted(bones), "inverseBindMatrices": a_ibm, "skeleton": 0}

    # ---------------- animations ----------------
    def sampler(times, values, atype, interp="LINEAR"):
        a_in = g.acc_f32(times, "SCALAR")
        a_out = g.acc_f32(values, atype)
        return {"input": a_in, "output": a_out, "interpolation": interp}

    def channel(smp_idx, node, path):
        return {"sampler": smp_idx, "target": {"node": node, "path": path}}

    anims = []
    # "idle": hips bob (known values for tests) + gentle sway
    samplers = []
    samplers.append(sampler([0.0, 0.5, 1.0, 1.5, 2.0],
                            [(0, 0.95, 0), (0, 0.96, 0), (0, 0.95, 0),
                             (0, 0.96, 0), (0, 0.95, 0)], "VEC3"))
    channels = [channel(0, 0, "translation")]
    samplers.append(sampler([0.0, 1.0, 2.0],
                            [quat_axis_angle(0, 0, 1, 0.0),
                             quat_axis_angle(0, 0, 1, 3.0),
                             quat_axis_angle(0, 0, 1, 0.0)], "VEC4"))
    channels.append(channel(1, 0, "rotation"))
    samplers.append(sampler([0.0, 1.0, 2.0],
                            [quat_axis_angle(0, 0, 1, 0.0),
                             quat_axis_angle(0, 0, 1, -2.0),
                             quat_axis_angle(0, 0, 1, 0.0)], "VEC4"))
    channels.append(channel(2, 2, "rotation"))
    anims.append({"name": "idle", "channels": channels, "samplers": samplers})

    # "wave": right arm up + forearm oscillation
    samplers = []
    samplers.append(sampler([0.0, 0.4, 2.4],
                            [quat_axis_angle(0, 0, 1, 0.0),
                             quat_axis_angle(0, 0, 1, -120.0),
                             quat_axis_angle(0, 0, 1, -120.0)], "VEC4"))
    channels = [channel(0, 7, "rotation")]
    keys_t, keys_v = [], []
    t = 0.4
    while t <= 2.4 + 1e-9:
        keys_t.append(round(t, 3))
        deg = 30.0 if (len(keys_t) % 2 == 1) else -10.0
        keys_v.append(quat_axis_angle(1, 0, 0, deg))
        t += 0.2
    samplers.append(sampler(keys_t, keys_v, "VEC4"))
    channels.append(channel(1, 8, "rotation"))
    samplers.append(sampler([0.0, 1.2, 2.4],
                            [quat_axis_angle(0, 1, 0, 0.0),
                             quat_axis_angle(0, 1, 0, -8.0),
                             quat_axis_angle(0, 1, 0, 0.0)], "VEC4"))
    channels.append(channel(2, 3, "rotation"))
    anims.append({"name": "wave", "channels": channels, "samplers": samplers})

    mat = {"name": "character_mat",
           "pbrMetallicRoughness": {"baseColorFactor": [0.85, 0.72, 0.45, 1.0],
                                     "metallicFactor": 0.05,
                                     "roughnessFactor": 0.6}}
    return g.build(nodes, [mesh], skins=[skin], animations=anims,
                   materials=[mat])


# ------------------------------------------------------------------ WAV
def write_wav(path, seconds=0.4, rate=44100, freq=440.0, amp=0.5):
    """A soft 'ping' instead of a raw sine: a few partials with a fast
    attack, an exponential decay and a fade at the end - so the loop
    point is silent and the tone never gets shrill."""
    frames = int(seconds * rate)
    data = bytearray()
    attack_n = max(1.0, 0.004 * rate)      # 4 ms
    release_n = max(1.0, 0.02 * rate)      # 20 ms
    for i in range(frames):
        t = i / rate
        env = 1.0 - math.exp(-i / attack_n)          # the attack
        env *= math.exp(-t * 9.0)                     # the decay
        env *= min(1.0, (frames - i) / release_n)     # the release
        v = (math.sin(2 * math.pi * freq * t) * 0.62
             + math.sin(2 * math.pi * freq * 2.0 * t) * 0.20
             + math.sin(2 * math.pi * freq * 3.0 * t) * 0.09)
        s = int(max(-1.0, min(1.0, v * env * amp)) * 32767.0)
        data += struct.pack("<h", s)
    hdr = b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE"
    hdr += b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, rate,
                                 rate * 2, 2, 16)
    hdr += b"data" + struct.pack("<I", len(data))
    with open(path, "wb") as f:
        f.write(hdr + bytes(data))


# ----------------------------------------------------------- text gltf
def write_triangle_gltf(dirpath):
    """plain .gltf with an external .bin - exercises the uri path of the loader"""
    pos = [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0)]
    nrm = [(0.0, 0.0, 1.0)] * 3
    idx = [0, 1, 2]
    bin_data = struct.pack("<9f", *[c for v in pos for c in v])
    bin_data += struct.pack("<9f", *[c for v in nrm for c in v])
    bin_data += struct.pack("<3H", *idx)
    with open(os.path.join(dirpath, "triangle.bin"), "wb") as f:
        f.write(bin_data)

    doc = {
        "asset": {"version": "2.0", "generator": "sdge make_assets.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": "tri"}],
        "meshes": [{"primitives": [{
            "attributes": {"POSITION": 0, "NORMAL": 1},
            "indices": 2, "mode": 4}]}],
        "accessors": [
            {"bufferView": 0, "componentType": FLOAT, "count": 3, "type": "VEC3",
             "min": [0.0, 0.0, 0.0], "max": [1.0, 1.0, 0.0]},
            {"bufferView": 1, "componentType": FLOAT, "count": 3, "type": "VEC3"},
            {"bufferView": 2, "componentType": UNSIGNED_SHORT, "count": 3,
             "type": "SCALAR"},
        ],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": 36},
            {"buffer": 0, "byteOffset": 36, "byteLength": 36},
            {"buffer": 0, "byteOffset": 72, "byteLength": 6},
        ],
        "buffers": [{"byteLength": len(bin_data), "uri": "triangle.bin"}],
    }
    with open(os.path.join(dirpath, "triangle.gltf"), "w") as f:
        json.dump(doc, f, separators=(",", ":"))


# ------------------------------------------------------------------ main
def main():
    subjects = os.path.join(ROOT, "Subjects")
    data = os.path.join(ROOT, "tests", "data")
    os.makedirs(subjects, exist_ok=True)
    os.makedirs(data, exist_ok=True)

    assets = [
        ("cube.glb", box(0, 0, 0, 1, 1, 1), "cube", (0.9, 0.25, 0.2)),
        ("sphere.glb", sphere(0.5), "sphere", (0.2, 0.5, 0.9)),
        ("plane.glb", plane(4.0, 4), "plane", (0.35, 0.4, 0.35)),
        ("cylinder.glb", cylinder(0.4, 1.0), "cylinder", (0.8, 0.7, 0.2)),
        ("cone.glb", cone(0.5, 1.0), "cone", (0.7, 0.3, 0.8)),
        ("torus.glb", torus(0.45, 0.16), "torus", (0.3, 0.85, 0.5)),
    ]
    for name, geom, gname, color in assets:
        path = os.path.join(subjects, name)
        with open(path, "wb") as f:
            f.write(simple_glb(geom, gname, color))
        print("wrote", path)

    path = os.path.join(subjects, "character.glb")
    with open(path, "wb") as f:
        f.write(character_glb())
    print("wrote", path)

    write_wav(os.path.join(subjects, "beep.wav"))
    print("wrote", os.path.join(subjects, "beep.wav"))
    write_wav(os.path.join(data, "beep.wav"))
    print("wrote", os.path.join(data, "beep.wav"))

    write_triangle_gltf(data)
    print("wrote", os.path.join(data, "triangle.gltf + .bin"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
