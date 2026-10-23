#!/usr/bin/env python3
"""Create a ROM-free, source-complete Linux ARM64 Frame package."""

from __future__ import annotations

import hashlib
import json
import os
import platform
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

from build_manifest import verify as verify_build_inputs

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
SDK_SOURCE = BUILD / "openxr-sdk-1.1.63" / "source"
PACKAGE_PARENT = BUILD / "package"
ROM_SUFFIXES = {".32x", ".gp0", ".mds", ".srm"}
OWNED_UNTRACKED_SOURCE = {
    "tools/skid_corner_inputs.txt", "src/vr_skids.c", "src/vr_skids.h", "tests/vr_skids_test.c", "tests/vr_skids_rom_test.c",
    "src/vr_xr.c", "src/vr_xr.h", "src/vr_math.c", "src/vr_math.h",
    "src/vr_render.inc", "src/sfvr/sfvr_view.c", "src/sfvr/sfvr_view.h",
    "tools/build_manifest.py", "tests/test_vr_contracts.py", "tests/vr_math_test.c", "packaging/launch_vr.sh", "licenses/sfvr-MIT.txt",
    "Dockerfile",
    "docs/STEAM_FRAME.md",
    "docs/VR_ARCHITECTURE.md",
    "licenses/G-Diffuser-MIT.txt",
    "packaging/launch_flat.sh",
    "packaging/launch_openxr_probe.sh",
    "tests/test_frame_launchers.sh",
    "tests/test_rom_preflight.py",
    "tools/build_frame.sh",
    "tools/openxr_colour_probe.c",
    "tools/package_frame.py",
    "tools/rom_preflight.py",
}
PACKAGE_PATHS = {
    "vrde-vr",
    "launch-vr.sh",
    "licenses/sfvr-MIT.txt",
    "vrde",
    "openxr-colour-probe",
    "launch-flat.sh",
    "launch-openxr-probe.sh",
    "rom_preflight.py",
    "README.txt",
    "LICENSE",
    "licenses/PicoDrive-COPYING",
    "licenses/emu2413-LICENSE",
    "licenses/G-Diffuser-MIT.txt",
    "licenses/OpenXR-SDK-LICENSE.txt",
    "source.tar.gz",
    "BUILD-METADATA.json",
    "SHA256SUMS",
}


def run(*args: str, cwd: Path = ROOT, text: bool = True) -> str | bytes:
    result = subprocess.run(args, cwd=cwd, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=text)
    return result.stdout


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def git_paths(*args: str) -> list[str]:
    raw = run("git", "ls-files", "-z", *args, text=False)
    assert isinstance(raw, bytes)
    return [item.decode("utf-8") for item in raw.split(b"\0") if item]


def source_file_list() -> list[str]:
    tracked = set(git_paths())
    untracked = set(git_paths("--others", "--exclude-standard"))
    selected = tracked | (untracked & OWNED_UNTRACKED_SOURCE)
    missing = sorted(path for path in OWNED_UNTRACKED_SOURCE if not (ROOT / path).is_file())
    if missing:
        raise RuntimeError(f"owned source paths are not regular files: {', '.join(missing)}")
    for path in selected:
        lower = path.lower()
        if Path(lower).suffix in ROM_SUFFIXES or "/rom/" in f"/{lower}/" or "/roms/" in f"/{lower}/":
            raise RuntimeError(f"refusing to package game or save data: {path}")
        if not (ROOT / path).is_file() or (ROOT / path).is_symlink():
            raise RuntimeError(f"source path is not a regular file: {path}")
    return sorted(selected)


def elf_machine(path: Path) -> int:
    data = path.read_bytes()[:20]
    if len(data) < 20 or data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        raise RuntimeError(f"{path.name} is not a 64-bit little-endian ELF binary")
    return struct.unpack_from("<H", data, 18)[0]


def runtime_dependencies(path: Path) -> list[dict[str, str]]:
    output = run("ldd", str(path), text=True)
    if "not found" in output:
        raise RuntimeError(f"unresolved runtime dependencies for {path.name}:\n{output}")
    found: dict[str, str] = {}
    for line in output.splitlines():
        fields = line.strip().split()
        if not fields:
            continue
        if "=>" in fields:
            index = fields.index("=>")
            soname = fields[0]
            resolved = fields[index + 1] if index + 1 < len(fields) else ""
        elif fields[0].startswith("/"):
            resolved = fields[0]
            soname = Path(resolved).name
        else:
            continue
        if resolved == "not":
            raise RuntimeError(f"unresolved dependency in ldd output for {path.name}: {line}")
        found[soname] = resolved
    return [{"soname": name, "resolved_in_builder": found[name]} for name in sorted(found)]


def status_snapshot() -> tuple[str, bool, list[str]]:
    revision = str(run("git", "rev-parse", "HEAD")).strip()
    dirty_paths: set[str] = set()
    for command in (
        ("git", "diff", "--name-only", "HEAD"),
        ("git", "ls-files", "--others", "--exclude-standard"),
    ):
        dirty_paths.update(str(run(*command)).splitlines())
    dirty_paths.discard("")
    return revision, bool(dirty_paths), sorted(dirty_paths)


def make_source_archive(destination: Path, source_paths: list[str]) -> None:
    with tarfile.open(destination, "w:gz", compresslevel=6) as archive:
        for relative in source_paths:
            archive.add(ROOT / relative, arcname=f"virtua-racing-vr/{relative}", recursive=False)


def check_package_contents(directory: Path) -> None:
    actual = {path.relative_to(directory).as_posix() for path in directory.rglob("*") if path.is_file()}
    if actual != PACKAGE_PATHS:
        raise RuntimeError(
            f"package allowlist mismatch; missing={sorted(PACKAGE_PATHS - actual)}, "
            f"unexpected={sorted(actual - PACKAGE_PATHS)}"
        )
    for relative in actual:
        if Path(relative).suffix.lower() in ROM_SUFFIXES:
            raise RuntimeError(f"game/save data found in package: {relative}")


def main() -> int:
    if platform.system() != "Linux" or platform.machine() not in {"aarch64", "arm64"}:
        raise RuntimeError(f"package target requires Linux/aarch64; got {platform.system()}/{platform.machine()}")

    binaries = {"vrde-vr": BUILD / "vrde-vr", "vrde": BUILD / "vrde", "openxr-colour-probe": BUILD / "openxr-colour-probe"}
    for name, binary in binaries.items():
        if not binary.is_file() or not os.access(binary, os.X_OK):
            raise RuntimeError(f"required executable missing: {binary}")
        if elf_machine(binary) != 183:
            raise RuntimeError(f"{name} is not an AArch64 ELF binary")

    openxr_license = SDK_SOURCE / "LICENSE"
    if not openxr_license.is_file():
        raise RuntimeError(f"pinned OpenXR SDK license missing: {openxr_license}")

    build_inputs = verify_build_inputs()
    revision, dirty, dirty_paths = status_snapshot()
    source_paths = source_file_list()
    source_fingerprint = hashlib.sha256()
    for relative in source_paths:
        source_fingerprint.update(relative.encode("utf-8") + b"\0")
        source_fingerprint.update(bytes.fromhex(sha256(ROOT / relative)))
    package_name = f"VirtuaRacingVR-{revision[:8]}-{source_fingerprint.hexdigest()[:8]}"
    PACKAGE_PARENT.mkdir(parents=True, exist_ok=True)
    destination = PACKAGE_PARENT / package_name
    if destination.exists():
        raise RuntimeError(f"refusing to overwrite existing package: {destination}")

    with tempfile.TemporaryDirectory(prefix="frame-package-", dir=PACKAGE_PARENT) as temp_name:
        stage = Path(temp_name)
        shutil.copy2(binaries["vrde"], stage / "vrde")
        shutil.copy2(binaries["vrde-vr"], stage / "vrde-vr")
        shutil.copy2(ROOT / "packaging/launch_vr.sh", stage / "launch-vr.sh")
        shutil.copy2(binaries["openxr-colour-probe"], stage / "openxr-colour-probe")
        shutil.copy2(ROOT / "packaging/launch_flat.sh", stage / "launch-flat.sh")
        shutil.copy2(ROOT / "packaging/launch_openxr_probe.sh", stage / "launch-openxr-probe.sh")
        shutil.copy2(ROOT / "tools/rom_preflight.py", stage / "rom_preflight.py")
        shutil.copy2(ROOT / "LICENSE", stage / "LICENSE")
        (stage / "licenses").mkdir()
        shutil.copy2(ROOT / "third_party/picodrive/COPYING", stage / "licenses/PicoDrive-COPYING")
        shutil.copy2(ROOT / "third_party/picodrive/pico/sound/emu2413/LICENSE", stage / "licenses/emu2413-LICENSE")
        shutil.copy2(ROOT / "licenses/G-Diffuser-MIT.txt", stage / "licenses/G-Diffuser-MIT.txt")
        shutil.copy2(ROOT / "licenses/sfvr-MIT.txt", stage / "licenses/sfvr-MIT.txt")
        shutil.copy2(openxr_license, stage / "licenses/OpenXR-SDK-LICENSE.txt")
        make_source_archive(stage / "source.tar.gz", source_paths)
        readme = """Virtua Racing Deluxe Enhanced — Steam Frame Linux ARM64

VR game entry point: ./launch-vr.sh [RAW_US_ROM_PATH]
Flat game entry point: ./launch-flat.sh [RAW_US_ROM_PATH]
Standalone OpenXR colour probe: ./launch-openxr-probe.sh [SECONDS]

The game requires your own raw US Virtua Racing Deluxe 32X image. The
launcher checks its exact size, MD5, and SHA-1 before starting the game. With
no argument, it looks for 'Virtua Racing Deluxe (USA).32x' in the persistent
VirtuaRacingVR data directory. Settings and saves are written there:
${XDG_DATA_HOME:-$HOME/.local/share}/VirtuaRacingVR.

The colour probe is a separate diagnostic executable. It needs the SteamVR
OpenXR runtime and an Xlib/GLX OpenGL 4.3 context. It does not read game data.
It clears the left and right eyes to distinct dim sRGB colours when the runtime
allows rendering; during standby it submits zero layers and continues polling.

Target: Linux aarch64 with SteamLinuxRuntime_4-arm64. Runtime libraries include
SDL2, OpenGL/GLX, X11, zlib, and the system C/C++ runtime. Python 3 is used by
the ROM preflight launcher. See BUILD-METADATA.json for the exact builder-side
dependency closure and source revision. source.tar.gz contains the matching
ROM-free source snapshot and dependency notices, including the adapted G-Diffuser
probe and its MIT license. Shared sfvr View logic is MIT, from astromaddie/sfvr
9932388f788ccbd71154ad3a766ac690b5bea669. See source docs/VR_ARCHITECTURE.md
for controls, world scale, diagnostics and renderer constraints.
"""
        (stage / "README.txt").write_text(readme, encoding="utf-8")

        dependencies = {name: runtime_dependencies(binary) for name, binary in binaries.items()}
        metadata = {
            "format": 1,
            "target": "Linux aarch64 / SteamLinuxRuntime_4-arm64",
            "source_revision": revision,
            "source_dirty": dirty,
            "source_dirty_paths": dirty_paths,
            "source_fingerprint_sha256": source_fingerprint.hexdigest(),
            "source_archive": "source.tar.gz",
            "build_inputs": build_inputs,
            "source_files": len(source_paths),
            "rom_included": False,
            "package_allowlist": sorted(PACKAGE_PATHS),
            "runtime_dependencies": dependencies,
            "openxr_sdk": {
                "version": "1.1.63",
                "archive_sha256": "5b8dac0608f49498fe8e95f562fe6bcf0bc781e561f137348a8c251d06a88b6f",
                "loader": "statically linked",
            },
        }
        artifacts = sorted(path for path in stage.rglob("*") if path.is_file())
        metadata["artifact_sha256"] = {
            path.relative_to(stage).as_posix(): sha256(path) for path in artifacts
        }
        (stage / "BUILD-METADATA.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
        sum_paths = sorted(path for path in stage.rglob("*") if path.is_file())
        sums = "".join(f"{sha256(path)}  {path.relative_to(stage).as_posix()}\n" for path in sum_paths)
        (stage / "SHA256SUMS").write_text(sums, encoding="utf-8")
        check_package_contents(stage)
        shutil.copytree(stage, destination)

    print(f"Package: {destination}")
    print(f"Source: {revision} (dirty={str(dirty).lower()}, {len(dirty_paths)} changed paths)")
    print(f"ROM-free allowlist: {len(PACKAGE_PATHS)} files; SHA256SUMS written")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"[package-frame] {error}", file=sys.stderr)
        raise SystemExit(1)
