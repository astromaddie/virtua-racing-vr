#!/usr/bin/env python3
import hashlib
import importlib.util
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


MODULE_PATH = Path(__file__).resolve().parents[1] / "tools" / "rom_preflight.py"
SPEC = importlib.util.spec_from_file_location("rom_preflight", MODULE_PATH)
rom_preflight = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(rom_preflight)


class RomPreflightTests(unittest.TestCase):
    def test_accepts_a_file_matching_the_production_hashing_path(self):
        content = b"production rom preflight path\0" * 3
        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "rom with spaces.32x"
            path.write_bytes(content)
            with (
                patch.object(rom_preflight, "EXPECTED_SIZE", len(content)),
                patch.object(rom_preflight, "EXPECTED_MD5", hashlib.md5(content, usedforsecurity=False).hexdigest()),
                patch.object(rom_preflight, "EXPECTED_SHA1", hashlib.sha1(content, usedforsecurity=False).hexdigest()),
            ):
                result = rom_preflight.preflight(path)
        self.assertEqual(result["path"], str(path.resolve()))
        self.assertEqual(result["size"], len(content))

    def test_rejects_wrong_size_before_hashing(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "short.32x"
            path.write_bytes(b"short")
            with self.assertRaisesRegex(rom_preflight.RomPreflightError, "Unsupported ROM size"):
                rom_preflight.preflight(path)

    def test_reports_missing_path(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "missing.32x"
            with self.assertRaisesRegex(rom_preflight.RomPreflightError, "ROM not found"):
                rom_preflight.preflight(path)

    def test_rejects_same_size_checksum_mismatch(self):
        content = b"x" * 64
        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "wrong.32x"
            path.write_bytes(content)
            with (
                patch.object(rom_preflight, "EXPECTED_SIZE", len(content)),
                patch.object(rom_preflight, "EXPECTED_MD5", "0" * 32),
                patch.object(rom_preflight, "EXPECTED_SHA1", "0" * 40),
            ):
                with self.assertRaisesRegex(rom_preflight.RomPreflightError, "checksum mismatch"):
                    rom_preflight.preflight(path)


if __name__ == "__main__":
    unittest.main()
