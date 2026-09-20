#!/usr/bin/env python3
"""Exercise source cache integrity and patch replacement without network access."""
import io
from pathlib import Path
import sys
import subprocess
import tarfile
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import sources


class SourceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.previous_cache = sources.CACHE
        sources.CACHE = self.root / "cache"
        self.archive = self.root / "upstream.tar.gz"
        with tarfile.open(self.archive, "w:gz") as archive:
            item = tarfile.TarInfo("upstream/source.txt")
            item.size = len(b"original\n")
            archive.addfile(item, io.BytesIO(b"original\n"))
        self.spec = {"url": self.archive.as_uri(), "sha256": sources.digest(self.archive),
                     "directory": "upstream"}
        self.patch = self.root / "change.patch"

    def tearDown(self):
        sources.CACHE = self.previous_cache
        self.temporary.cleanup()

    def test_patch_replacement_restores_removed_files_and_is_idempotent(self):
        self.patch.write_text("--- a/source.txt\n+++ b/source.txt\n@@ -1 +1 @@\n-original\n+first\n"
                              "--- /dev/null\n+++ b/added.txt\n@@ -0,0 +1 @@\n+new\n")
        dependency = sources.Source(self.spec, [self.patch], self.root / "source")
        directory = dependency.prepare()
        self.assertEqual((directory / "source.txt").read_text(), "first\n")
        self.assertEqual((directory / "added.txt").read_text(), "new\n")
        stamp = (directory / "source.txt").stat().st_mtime_ns
        dependency.prepare()
        self.assertEqual((directory / "source.txt").stat().st_mtime_ns, stamp)
        self.patch.write_text("--- a/source.txt\n+++ b/source.txt\n@@ -1 +1 @@\n-original\n+second\n")
        dependency.prepare()
        self.assertEqual((directory / "source.txt").read_text(), "second\n")
        self.assertFalse((directory / "added.txt").exists())

    def test_failed_patch_does_not_publish_or_change_existing_source(self):
        dependency = sources.Source(self.spec, destination=self.root / "source")
        directory = dependency.prepare()
        marker = (directory / ".plant-source-sha256").read_bytes()
        self.patch.write_text("--- a/source.txt\n+++ b/source.txt\n@@ -1 +1 @@\n-wrong\n+bad\n")
        with self.assertRaises(subprocess.CalledProcessError):
            sources.Source(self.spec, [self.patch], self.root / "source").prepare()
        self.assertEqual((directory / "source.txt").read_text(), "original\n")
        self.assertEqual((directory / ".plant-source-sha256").read_bytes(), marker)

    def test_download_rejects_wrong_checksum(self):
        with self.assertRaisesRegex(RuntimeError, "checksum mismatch"):
            sources.Source({**self.spec, "sha256": "0" * 64}).download()
        self.assertFalse((sources.CACHE / self.archive.name).exists())
        self.assertFalse(list(sources.CACHE.glob("*.part")))


if __name__ == "__main__":
    unittest.main()
