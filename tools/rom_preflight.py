#!/usr/bin/env python3
"""Validate the user-supplied raw US Virtua Racing Deluxe 32X image."""

from __future__ import annotations

import hashlib
import os
import sys
from pathlib import Path

EXPECTED_SIZE = 3_145_728
EXPECTED_MD5 = "72b1ad0f949f68da7d0a6339ecd51a3f"
EXPECTED_SHA1 = "18dfdeb50780c2623e60a6587d7ed701a1cf81f1"
CHUNK_SIZE = 1024 * 1024


class RomPreflightError(ValueError):
    """The supplied file is missing or is not the supported raw game image."""


def _hash_file(path: Path) -> tuple[str, str]:
    md5 = hashlib.md5(usedforsecurity=False)
    sha1 = hashlib.sha1(usedforsecurity=False)
    with path.open("rb") as stream:
        while chunk := stream.read(CHUNK_SIZE):
            md5.update(chunk)
            sha1.update(chunk)
    return md5.hexdigest(), sha1.hexdigest()


def preflight(path: str | os.PathLike[str]) -> dict[str, str | int]:
    rom = Path(path).expanduser()
    if not rom.exists():
        raise RomPreflightError(f"ROM not found: {rom}")
    if not rom.is_file():
        raise RomPreflightError(f"ROM path is not a regular file: {rom}")

    size = rom.stat().st_size
    if size != EXPECTED_SIZE:
        raise RomPreflightError(
            f"Unsupported ROM size: {size} bytes; expected {EXPECTED_SIZE} bytes "
            "for the raw US image (ZIP archives and extracted alternatives are not accepted)."
        )

    md5, sha1 = _hash_file(rom)
    if md5 != EXPECTED_MD5 or sha1 != EXPECTED_SHA1:
        raise RomPreflightError(
            "ROM checksum mismatch; expected the US raw image "
            f"(MD5 {EXPECTED_MD5}, SHA-1 {EXPECTED_SHA1}), got "
            f"(MD5 {md5}, SHA-1 {sha1})."
        )
    return {"path": str(rom.resolve()), "size": size, "md5": md5, "sha1": sha1}


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(f"Usage: {Path(argv[0]).name} ROM_PATH", file=sys.stderr)
        return 2
    try:
        result = preflight(argv[1])
    except (OSError, RomPreflightError) as exc:
        print(f"[rom-preflight] {exc}", file=sys.stderr)
        return 1
    print(
        f"[rom-preflight] PASS size={result['size']} md5={result['md5']} "
        f"sha1={result['sha1']} path={result['path']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
