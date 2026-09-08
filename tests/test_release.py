"""Release contracts: versioning, changelog extraction and architecture-labelled assets."""

import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location("release", Path(__file__).resolve().parents[1] / "scripts/release.py")
release = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release)


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "CMakeLists.txt").write_text("project(\n HLaunch\n VERSION 0.1.19\n LANGUAGES CXX\n)\n", encoding="utf-8")
        (self.root / "CHANGELOG.md").write_text(
            "# Changelog\n\n## [Unreleased]\n\n### 中文\n\n- 新功能。\n\n### English\n\n- New feature.\n\n"
            "## [0.1.19] - 2026-08-29\n\n- Icon fix.\n", encoding="utf-8")

    def test_prepare_increments_patch_and_preserves_history(self):
        self.assertEqual(release.prepare(self.root, "2026-09-08"), "0.1.20")
        version, notes = release.validate(self.root, "v0.1.20")
        self.assertEqual(version, "0.1.20")
        self.assertIn("- 新功能。", notes)
        self.assertIn("- New feature.", notes)
        self.assertNotIn("Icon fix", notes)
        text = (self.root / "CHANGELOG.md").read_text(encoding="utf-8")
        self.assertIn("## [0.1.20] - 2026-09-08", text)
        self.assertIn("- Icon fix.", text)
        with self.assertRaises(ValueError):
            release.prepare(self.root)
        self.assertEqual(release.project_version(self.root), "0.1.20")

    def test_reject_invalid_tags_and_version_mismatch(self):
        for tag in ["main", "v0.1", "0.1.19", "v0.1.19-beta", "v0.1.19.0", "v00.1.19", "v0.1.20", "v0.1.19\n"]:
            with self.subTest(tag=tag), self.assertRaises(ValueError):
                release.validate(self.root, tag)

    def test_release_requires_unique_nonempty_notes(self):
        path = self.root / "CHANGELOG.md"
        for content in ["## [Unreleased]\n- Future\n", "## [0.1.19]\n\n### English\n", "## [0.1.19]\n- A\n## [0.1.19]\n- B\n"]:
            with self.subTest(content=content):
                path.write_text(content, encoding="utf-8")
                with self.assertRaises(ValueError):
                    release.validate(self.root, "v0.1.19")

    def test_patch_overflow_leaves_files_unchanged(self):
        cmake = self.root / "CMakeLists.txt"
        cmake.write_text("project(HLaunch VERSION 0.1.65535)\n", encoding="utf-8")
        before = {path: path.read_bytes() for path in self.root.iterdir()}
        with self.assertRaises(ValueError):
            release.prepare(self.root)
        self.assertEqual(before, {path: path.read_bytes() for path in self.root.iterdir()})

    def write_pe(self, arch, machine=None):
        data = bytearray(134)
        data[:2] = b"MZ"
        struct.pack_into("<I", data, 0x3C, 128)
        data[128:132] = b"PE\0\0"
        struct.pack_into("<H", data, 132, machine or release.MACHINES[arch])
        (self.root / f"HLaunch-0.1.19-{arch}.exe").write_bytes(data)

    def test_assets_require_all_three_matching_architectures(self):
        self.write_pe("x64")
        self.write_pe("x86")
        with self.assertRaises(ValueError):
            release.checksums(self.root, "0.1.19")
        self.write_pe("arm64", release.MACHINES["x64"])
        with self.assertRaises(ValueError):
            release.checksums(self.root, "0.1.19")
        self.write_pe("arm64")
        release.checksums(self.root, "0.1.19")
        lines = (self.root / "SHA256SUMS.txt").read_text().splitlines()
        self.assertEqual(len(lines), 3)
        for line in lines:
            digest, name = line.split("  ")
            self.assertEqual(len(digest), 64)
            self.assertTrue((self.root / name).is_file())


if __name__ == "__main__":
    unittest.main()
