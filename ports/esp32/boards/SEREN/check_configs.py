#!/usr/bin/env python3
"""Validate the SEREN board matrix.

Run from anywhere:

    python3 ports/esp32/boards/SEREN/check_configs.py

Static checks (always run), for every SEREN_* board directory:
  * every file referenced from *.cmake exists (sdkconfig fragments, partition
    CSVs, user C modules, shared-layer files)
  * every CONFIG_PARTITION_TABLE_CUSTOM_FILENAME target exists
  * board.json "deploy" paths exist and "variants" keys match the
    mpconfigvariant_*.cmake files (the legacy OTA alias is exempt; every other
    variant file must be listed in board.json)
  * every seren_profile(NAME) has a matching boards/SEREN/profiles/NAME.py
  * the manifest chain resolves for the default profile and every variant
    profile: manifest.py -> profiles/<profile>.py -> manifest_common.py

Optional deeper checks:
  --manifests [WORKSPACE]  also execute every manifest combination with
      tools/makemanifest.py (needs mpy-cross built: make -C mpy-cross, and a
      WORKSPACE_PATH; defaults to the fallback worktree in seren_common.cmake)
  --configure  also run a cmake configure-only pass for every variant (needs an
      exported ESP-IDF environment)

Exits non-zero if any check fails.
"""

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))  # boards/SEREN
BOARDS_DIR = os.path.dirname(SCRIPT_DIR)  # boards
PORT_DIR = os.path.dirname(BOARDS_DIR)  # ports/esp32
MPY_DIR = os.path.dirname(os.path.dirname(PORT_DIR))  # repository root

PROFILES_DIR = os.path.join(SCRIPT_DIR, "profiles")
DEFAULT_WORKSPACE = "/home/alexx/Zen-Specs/Code_device.worktrees/dev"
LEGACY_VARIANTS = {"OTA"}  # legacy alias for the default profile

errors = []
checks = 0


def ok(msg):
    global checks
    checks += 1


def error(msg):
    global checks
    checks += 1
    errors.append(msg)
    print("ERROR:", msg)


def strip_comment(line):
    return line.split("#", 1)[0]


def seren_boards():
    return sorted(
        d
        for d in os.listdir(BOARDS_DIR)
        if d.startswith("SEREN_") and os.path.isdir(os.path.join(BOARDS_DIR, d))
    )


def variant_names(board):
    """Profile-ish variant names from mpconfigvariant_<name>.cmake files."""
    names = []
    for f in sorted(os.listdir(os.path.join(BOARDS_DIR, board))):
        m = re.fullmatch(r"mpconfigvariant_(.+)\.cmake", f)
        if m:
            names.append(m.group(1))
    return names


def cmake_path_vars(board):
    board_dir = os.path.join(BOARDS_DIR, board)
    return {
        "MICROPY_BOARD_DIR": board_dir,
        "SEREN_COMMON_DIR": SCRIPT_DIR,
        "CMAKE_CURRENT_LIST_DIR": board_dir,
        "_MICROPY_ROOT": MPY_DIR,
        "MICROPY_DIR": MPY_DIR,
    }


CMAKE_BARE_PATH_RE = re.compile(r"(?<![\w$/])boards/[A-Za-z0-9_./-]+")
CMAKE_VAR_PATH_RE = re.compile(r"\$\{(\w+)\}(/[A-Za-z0-9_./-]*)")
SEREN_PROFILE_RE = re.compile(r"seren_profile\(\s*(\w+)\s*\)")
PARTITION_CSV_RE = re.compile(r'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="([^"]+)"')


def check_cmake_references(board):
    board_dir = os.path.join(BOARDS_DIR, board)
    path_vars = cmake_path_vars(board)
    targets = [os.path.join(SCRIPT_DIR, "seren_common.cmake")]
    for f in sorted(os.listdir(board_dir)):
        if f.endswith(".cmake"):
            targets.append(os.path.join(board_dir, f))
    for path in targets:
        with open(path) as f:
            for lineno, line in enumerate(f, 1):
                text = strip_comment(line)
                for ref in CMAKE_BARE_PATH_RE.findall(text):
                    if not os.path.exists(os.path.join(PORT_DIR, ref)):
                        error(f"{path}:{lineno}: missing {ref}")
                    else:
                        ok(ref)
                for var, suffix in CMAKE_VAR_PATH_RE.findall(text):
                    if var not in path_vars or not suffix:
                        continue
                    full = os.path.join(path_vars[var], suffix.lstrip("/"))
                    if not os.path.exists(full):
                        error(f"{path}:{lineno}: missing ${{{var}}}{suffix}")
                    else:
                        ok(suffix)
                for profile in SEREN_PROFILE_RE.findall(text):
                    check_profile_exists(profile, f"{path}:{lineno}")


def check_profile_exists(profile, origin):
    path = os.path.join(PROFILES_DIR, profile + ".py")
    if os.path.exists(path):
        ok(path)
    else:
        error(f"{origin}: unknown profile {profile!r} (expected {path})")


def check_partition_tables():
    for base in [SCRIPT_DIR] + [os.path.join(BOARDS_DIR, b) for b in seren_boards()]:
        for f in sorted(os.listdir(base)):
            if not f.startswith("sdkconfig"):
                continue
            with open(os.path.join(base, f)) as fh:
                for lineno, line in enumerate(fh, 1):
                    for csv in PARTITION_CSV_RE.findall(line):
                        full = os.path.join(PORT_DIR, csv)
                        if os.path.exists(full):
                            ok(csv)
                        else:
                            error(f"{os.path.join(base, f)}:{lineno}: missing {csv}")


def check_board_json(board):
    board_dir = os.path.join(BOARDS_DIR, board)
    with open(os.path.join(board_dir, "board.json")) as f:
        meta = json.load(f)
    for deploy in meta.get("deploy", []):
        full = os.path.join(board_dir, deploy)
        if os.path.exists(full):
            ok(deploy)
        else:
            error(f"{board}/board.json: missing deploy doc {deploy}")
    listed = set(meta.get("variants", {}))
    present = set(variant_names(board))
    for name in sorted(listed - present):
        error(f"{board}/board.json: variant {name!r} has no mpconfigvariant_{name}.cmake")
    for name in sorted(present - listed - LEGACY_VARIANTS):
        error(f"{board}: mpconfigvariant_{name}.cmake is not listed in board.json variants")
    ok(board)


INCLUDE_RE = re.compile(r'include\(\s*"([^"]+)"')


def check_manifest_chain(board, profile):
    """manifest.py -> profiles/<profile>.py -> manifest_common.py must resolve."""
    path = os.path.join(BOARDS_DIR, board, "manifest.py")
    substitutions = {"$(SEREN_PROFILE)": profile}
    seen = set()
    while True:
        if not os.path.exists(path):
            error(f"{board}/{profile}: missing manifest include {path}")
            return
        ok(path)
        if path in seen:
            return
        seen.add(path)
        with open(path) as f:
            includes = INCLUDE_RE.findall(f.read())
        next_path = None
        for inc in includes:
            for key, val in substitutions.items():
                inc = inc.replace(key, val)
            if "$(" in inc:
                continue  # resolved from build variables, not statically known
            full = os.path.normpath(os.path.join(os.path.dirname(path), inc))
            if os.path.isdir(full):
                full = os.path.join(full, "manifest.py")
            if not os.path.exists(full):
                error(f"{path}: missing include {inc}")
            elif next_path is None:
                next_path = full
        if next_path is None:
            return
        path = next_path


def profiles_for(board):
    """Default profile (mpconfigboard.cmake) plus all variant profiles."""
    board_dir = os.path.join(BOARDS_DIR, board)
    profiles = []
    for f in sorted(os.listdir(board_dir)):
        if f == "mpconfigboard.cmake" or re.fullmatch(r"mpconfigvariant_.+\.cmake", f):
            with open(os.path.join(board_dir, f)) as fh:
                for line in fh:
                    profiles += SEREN_PROFILE_RE.findall(strip_comment(line))
    return sorted(set(profiles))


def run_manifests(workspace):
    mpy_cross = os.path.join(MPY_DIR, "mpy-cross", "build", "mpy-cross")
    if not os.path.exists(mpy_cross):
        error("--manifests: mpy-cross not built (run: make -C mpy-cross)")
        return
    if not os.path.isdir(workspace):
        error(f"--manifests: workspace not found: {workspace}")
        return
    combos = [(b, p) for b in seren_boards() for p in profiles_for(b)]
    if not combos:
        error("--manifests: no board/profile combinations resolved")
        return
    with tempfile.TemporaryDirectory() as tmp:
        # Stand-in for the build-generated qstr header so mpy-tool can merge the
        # frozen modules (values match mpy-tool's own defaults).
        genhdr = os.path.join(tmp, "genhdr")
        os.mkdir(genhdr)
        with open(os.path.join(genhdr, "qstrdefs.preprocessed.h"), "w") as f:
            f.write("QCFG(BYTES_IN_LEN, 1)\nQCFG(BYTES_IN_HASH, 1)\n")
        # Stand-in for the build-generated build_variant.py (frozen by
        # manifest_common.py from $(BUILD_DIR)).
        with open(os.path.join(tmp, "build_variant.py"), "w") as f:
            f.write('VARIANT = ""\n')
        for board, profile in combos:
            cmd = [
                sys.executable,
                os.path.join(MPY_DIR, "tools", "makemanifest.py"),
                "-o", os.path.join(tmp, "frozen.c"),
                "-b", tmp,
                "-v", "MPY_DIR=" + MPY_DIR,
                "-v", "PORT_DIR=" + PORT_DIR,
                "-v", "MPY_LIB_DIR=" + os.path.join(MPY_DIR, "lib", "micropython-lib"),
                "-v", "WORKSPACE_PATH=" + workspace,
                "-v", "SEREN_PROFILE=" + profile,
                "-v", "BUILD_DIR=" + tmp,
                os.path.join(BOARDS_DIR, board, "manifest.py"),
            ]
            res = subprocess.run(cmd, capture_output=True, text=True)
            combo = f"{board}/{profile}"
            if res.returncode != 0:
                error(f"--manifests: {combo} failed:\n{res.stdout}{res.stderr}")
            else:
                print(f"  manifest OK: {combo}")
                ok(combo)


def run_configure():
    if not os.environ.get("IDF_PATH"):
        error("--configure: ESP-IDF not exported (IDF_PATH is unset)")
        return
    with tempfile.TemporaryDirectory() as tmp:
        for board in seren_boards():
            combos = [(None, "default")] + [(v, v) for v in variant_names(board)]
            for variant, label in combos:
                build = os.path.join(tmp, f"{board}-{label}")
                cmd = [
                    "cmake", "-S", PORT_DIR, "-B", build,
                    "-D", f"MICROPY_BOARD={board}",
                    "-D", f"MICROPY_MANIFEST_WORKSPACE_PATH={DEFAULT_WORKSPACE}",
                ]
                if variant:
                    cmd += ["-D", f"MICROPY_BOARD_VARIANT={variant}"]
                res = subprocess.run(cmd, capture_output=True, text=True)
                combo = f"{board}/{label}"
                if res.returncode != 0:
                    error(f"--configure: {combo} failed:\n{res.stdout}{res.stderr}")
                else:
                    print(f"  configure OK: {combo}")
                    ok(combo)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--manifests",
        nargs="?",
        const=DEFAULT_WORKSPACE,
        metavar="WORKSPACE",
        help="execute every manifest combination against WORKSPACE",
    )
    parser.add_argument(
        "--configure",
        action="store_true",
        help="cmake configure-only smoke pass (needs exported ESP-IDF)",
    )
    args = parser.parse_args()

    for board in seren_boards():
        print(f"checking {board} ...")
        check_cmake_references(board)
        check_board_json(board)
        profiles = profiles_for(board)
        if not profiles:
            error(f"{board}: no seren_profile() found in board cmake files")
        for profile in profiles:
            check_manifest_chain(board, profile)
    check_partition_tables()

    if args.manifests is not None:
        run_manifests(args.manifests)
    if args.configure:
        run_configure()

    print(f"\n{checks} checks, {len(errors)} errors")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
