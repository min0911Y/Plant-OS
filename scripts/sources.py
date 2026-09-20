"""Pinned upstream sources and atomic, reproducible patch application."""
import fcntl
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / "apps/out/sources"


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def publish(path, contents):
    if path.exists() and path.read_text() == contents:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(contents)
    temporary.replace(path)


class Source:
    def __init__(self, spec, patches=(), destination=CACHE):
        self.spec = spec
        self.patches = tuple(patches)
        self.destination = destination

    def download(self):
        CACHE.mkdir(parents=True, exist_ok=True)
        name = self.spec.get("filename", self.spec["url"].rsplit("/", 1)[1])
        archive = CACHE / name
        if archive.is_file() and digest(archive) == self.spec["sha256"]:
            return archive
        with tempfile.NamedTemporaryFile(dir=CACHE, suffix=".part", delete=False) as stream:
            temporary = Path(stream.name)
            try:
                print(f"Downloading {name}", flush=True)
                with urllib.request.urlopen(self.spec["url"], timeout=120) as response:
                    shutil.copyfileobj(response, stream)
                stream.close()
                if digest(temporary) != self.spec["sha256"]:
                    raise RuntimeError(f"checksum mismatch: {name}")
                temporary.replace(archive)
            finally:
                temporary.unlink(missing_ok=True)
        return archive

    def prepare(self):
        if "directory" not in self.spec:
            return self.download()
        CACHE.mkdir(parents=True, exist_ok=True)
        with (CACHE / ".lock").open("w") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            directory = self.destination / self.spec["directory"]
            files = set()
            for patch in self.patches:
                for line in patch.read_text().splitlines():
                    if line.startswith(("--- a/", "+++ b/")):
                        files.add(line[6:].split("\t", 1)[0])
            state = {"archive": self.spec["sha256"],
                     "patch": hashlib.sha256(b"".join(p.read_bytes() for p in self.patches)).hexdigest(),
                     "files": sorted(files)}
            marker = directory / ".plant-source-sha256"
            previous = json.loads(marker.read_text()) if marker.exists() else None
            if previous == state:
                return directory
            archive = self.download()
            self.destination.mkdir(parents=True, exist_ok=True)
            if not directory.exists():
                with tempfile.TemporaryDirectory(dir=self.destination, prefix="extract-") as temporary:
                    with tarfile.open(archive) as package:
                        package.extractall(temporary, filter="data")
                    (Path(temporary) / self.spec["directory"]).replace(directory)
            files.update(previous["files"] if previous else ())
            for relative in files:
                if Path(relative).is_absolute() or ".." in Path(relative).parts:
                    raise RuntimeError(f"invalid patch path: {relative}")
            if files:
                with tempfile.TemporaryDirectory(dir=CACHE, prefix="patch-") as temporary:
                    staging = Path(temporary)
                    prefix = self.spec["directory"] + "/"
                    # Stream once, even for the large LLVM/OpenJDK archives.
                    with tarfile.open(archive, "r|*") as package:
                        for member in package:
                            relative = member.name.removeprefix(prefix)
                            if not member.name.startswith(prefix) or relative not in files:
                                continue
                            if not member.isfile():
                                raise RuntimeError(f"patch target is not a regular file: {relative}")
                            path = staging / relative
                            path.parent.mkdir(parents=True, exist_ok=True)
                            path.write_bytes(package.extractfile(member).read())
                            path.chmod(member.mode & 0o777)
                    for patch in self.patches:
                        subprocess.run(["patch", "--batch", "-p1", "-i", str(patch)], cwd=staging, check=True)
                    for relative in files:
                        staged, target = staging / relative, directory / relative
                        if not staged.exists():
                            target.unlink(missing_ok=True)
                        elif not target.exists() or target.read_bytes() != staged.read_bytes():
                            target.parent.mkdir(parents=True, exist_ok=True)
                            staged.replace(target)
            publish(marker, json.dumps(state, sort_keys=True) + "\n")
            return directory


class Sources:
    def __init__(self, port):
        self.port = port
        self.specs = json.loads((port / "sources.json").read_text())

    def __getitem__(self, name):
        spec = self.specs[name]
        patches = sorted((self.port / "patches" / name).glob("*.patch"))
        destination = ROOT / spec.get("destination", "apps/out/sources")
        return Source(spec, patches, destination)
