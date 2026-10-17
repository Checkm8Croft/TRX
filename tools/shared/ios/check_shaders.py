#!/usr/bin/env python3
"""Checks that the shaders still build for GLSL ES 3.00 (all iOS offers).

Run from the repository root after merging upstream:

    python3 tools/shared/ios/check_shaders.py

Needs glslangValidator (brew install glslang).

It does two things:
  1. Checks the "ES varyings" lists in meshes.glsl against MESH_VARYINGS.
     Upstream passes the mesh stages' data as an interface block, which GLSL ES
     3.00 doesn't have, so meshes.glsl repeats the members as plain varyings
     under GL_ES. A member added upstream has to be added there too.
  2. Compiles and links every shader program as GLSL ES 3.00, using the same
     preamble gl/program.c prepends on iOS.

This cannot stand in for the device: Apple's compiler is stricter than
glslang in places (it doesn't expand a macro name passed as a macro argument,
for one). Keep the GL_ES code in the shaders to plain constructs.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

SHADER_DIR = "data/trx/ship/cfg/shaders"
PROGRAMS = [
    "meshes_tr12.glsl", "meshes_tr3.glsl", "meshes_tr4.glsl",
    "ui.glsl", "2d.glsl", "fbo.glsl", "fx_blit.glsl",
]
# Keep in step with M_Preprocess() in src/trx/gl/program.c (iOS branch).
PREAMBLE = (
    "#version 300 es\n"
    "precision highp float;\n"
    "precision highp int;\n"
    "precision highp sampler2D;\n"
    "precision highp sampler2DArray;\n"
    "precision highp sampler3D;\n"
    "#define noperspective\n"
)


def expand(src, directory):
    out, pos = [], 0
    while True:
        m = re.search(r'#include\s+"([^"]+)"', src[pos:])
        if not m:
            out.append(src[pos:])
            return "".join(out)
        out.append(src[pos:pos + m.start()])
        path = os.path.join(directory, m.group(1))
        with open(path) as f:
            out.append(expand(f.read(), os.path.dirname(path)))
        pos += m.end()


def check_varyings():
    with open(os.path.join(SHADER_DIR, "meshes.glsl")) as f:
        text = f.read()
    block = re.search(r"#define MESH_VARYINGS(.*?)\n\n", text, re.S).group(1)
    upstream = {}
    for line in block.replace("\\", "").split(";"):
        words = line.split()
        if len(words) >= 2:
            upstream[words[-1]] = (words[-2], "flat" in words)

    problems = []

    def section(tag):
        m = re.search(r"// ES varyings \(%s[^\n]*\n(.*?)(?:\n\s*\n|\n\s*(?:MeshVaryings|\}|#define main|trx_mesh))"
                      % re.escape(tag), text, re.S)
        return m.group(1) if m else ""

    members = {n: t for t, n in re.findall(r"^\s*(\w+) (\w+);$", section("1/5"), re.M)}
    for name, (typ, _) in upstream.items():
        if members.get(name) != typ:
            problems.append(f"struct MeshVaryings: '{name}' ({typ}) missing or mistyped")
    for name in members:
        if name not in upstream:
            problems.append(f"struct MeshVaryings: '{name}' is not in MESH_VARYINGS")

    for tag, kw in (("2/5", "out"), ("4/5", "in")):
        decls = re.findall(r"^\s*(flat )?%s (\w+) (\w+);$" % kw, section(tag), re.M)
        got = {v: (t, bool(f)) for f, t, v in decls}
        if len(got) != len(upstream):
            problems.append(f"{tag}: {len(got)} varyings, MESH_VARYINGS has {len(upstream)}")
        for name, (typ, flat) in upstream.items():
            if not any(t == typ and fl == flat for t, fl in got.values()):
                problems.append(f"{tag}: no '{'flat ' if flat else ''}{kw} {typ}' for '{name}'")

    for tag, pattern in (("3/5", r"= gOut\.(\w+);"), ("5/5", r"gIn\.(\w+) =")):
        names = set(re.findall(pattern, section(tag)))
        for name in upstream:
            if name not in names:
                problems.append(f"{tag}: '{name}' is not copied")
    return problems


def check_compile():
    glslang = shutil.which("glslangValidator")
    if glslang is None:
        return ["glslangValidator not found (brew install glslang)"]
    problems = []
    with tempfile.TemporaryDirectory() as tmp:
        for entry in PROGRAMS:
            with open(os.path.join(SHADER_DIR, entry)) as f:
                body = expand(f.read(), SHADER_DIR)
            files = []
            for stage, ext in (("VERTEX", "vert"), ("FRAGMENT", "frag")):
                path = os.path.join(tmp, f"{entry}.{ext}")
                with open(path, "w") as f:
                    f.write(PREAMBLE + f"#define {stage}\n" + body)
                files.append(path)
            result = subprocess.run([glslang, "-l"] + files, capture_output=True, text=True)
            if result.returncode != 0:
                problems.append(f"{entry}:\n" + "\n".join((result.stdout + result.stderr).splitlines()[:15]))
    return problems


def main():
    if not os.path.isdir(SHADER_DIR):
        sys.exit("Run this from the repository root.")
    problems = check_varyings() + check_compile()
    for p in problems:
        print("FAIL:", p)
    print("shaders OK for GLSL ES 3.00" if not problems else f"{len(problems)} problem(s)")
    sys.exit(1 if problems else 0)


main()
