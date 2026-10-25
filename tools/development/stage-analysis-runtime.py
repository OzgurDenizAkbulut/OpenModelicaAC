#!/usr/bin/env python3
"""Stage an optional, isolated Windows analysis runtime; never downloads packages.

The source interpreter must already provide Matplotlib, NumPy and Tornado.
A manifest records package versions and SHA-256 hashes of all shipped files.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

PROBE = r'''
import importlib.metadata as metadata, json, sys, sysconfig
from packaging.requirements import Requirement
pending = ["matplotlib", "numpy", "tornado", "packaging"]
seen = {}
while pending:
    name = pending.pop()
    dist = metadata.distribution(name)
    key = dist.metadata["Name"].lower().replace("_", "-")
    if key in seen:
        continue
    files = [str(dist.locate_file(p).resolve()) for p in (dist.files or [])]
    seen[key] = {"version": dist.version, "root": str(dist.locate_file("").resolve()), "files": files}
    for text in dist.requires or []:
        req = Requirement(text)
        if req.marker is None or req.marker.evaluate({"extra": ""}):
            pending.append(req.name)
print(json.dumps({"prefix": sys.base_prefix, "version": sys.version,
 "stdlib": sysconfig.get_path("stdlib"), "packages": seen}))
'''
VALIDATE = r'''
import io, json, pathlib, sys
root = pathlib.Path(sys.argv[1]).resolve(strict=True)
def contained(path, description):
    assert pathlib.Path(path).resolve().is_relative_to(root), description + " escapes the requested runtime: " + str(path)
contained(sys.executable, "Interpreter")
for name in ("prefix", "base_prefix", "exec_prefix", "base_exec_prefix"):
    contained(getattr(sys, name), "Python " + name)
assert sys.flags.isolated, "Interpreter did not enter isolated mode"
import numpy, tornado, tornado.websocket
import matplotlib
matplotlib.use("Agg")
from matplotlib.figure import Figure
from matplotlib.backends.backend_agg import FigureCanvasAgg
from matplotlib.backends import backend_webagg
fig = Figure(); FigureCanvasAgg(fig); fig.add_subplot().plot([0, 1], [1, 0])
stream = io.BytesIO(); fig.savefig(stream, format="png")
assert stream.getvalue().startswith(b"\x89PNG"), "PNG rendering failed"
for module in tuple(sys.modules.values()):
    path = getattr(module, "__file__", None)
    if path and not path.startswith("<"):
        contained(path, "Imported module")
    for path in getattr(module, "__path__", ()):
        contained(path, "Package search path")
for path in sys.path:
    if path:
        contained(path, "Python search path")
print(json.dumps({"python": sys.version.split()[0], "matplotlib": matplotlib.__version__,
 "numpy": numpy.__version__, "tornado": tornado.version, "isolated": sys.flags.isolated,
 "relocationTested": False}))
'''


def execute(python, code, isolated=True, arguments=()):
    env = os.environ.copy()
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    for key in ("PYTHONHOME", "PYTHONPATH", "PYTHONUSERBASE"):
        env.pop(key, None)
    with tempfile.TemporaryDirectory(prefix="omedit-analysis-check-") as temp:
        env["MPLCONFIGDIR"] = temp
        process = subprocess.run([str(python)] + (["-I"] if isolated else []) + ["-B", "-c", code, *map(str, arguments)], env=env,
                                 cwd=temp, text=True, capture_output=True, timeout=60)
    if process.returncode:
        raise RuntimeError(process.stderr or process.stdout)
    return process.stdout.strip()


def executable(root):
    return root / ("python.exe" if os.name == "nt" else "bin/python3")


def runtime_paths(root):
    for path in sorted(root.rglob("*")):
        if not path.resolve(strict=True).is_relative_to(root):
            raise RuntimeError(f"Runtime path escapes the runtime directory: {path}")
        yield path


def hashes(root):
    root = root.resolve(strict=True)
    return {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in runtime_paths(root)
            if p.is_file() and "__pycache__" not in p.relative_to(root).parts
            and p != root / "analysis-runtime-manifest.json"}


def validate(root):
    root = root.resolve(strict=True)
    python = executable(root)
    if not python.resolve(strict=True).is_relative_to(root):
        raise RuntimeError(f"Interpreter escapes the runtime directory: {python}")
    actual = hashes(root)
    manifest = root / "analysis-runtime-manifest.json"
    if manifest.exists():
        content = json.loads(manifest.read_text(encoding="utf-8"))
        if not isinstance(content, dict) or content.get("version") != 1 or not isinstance(content.get("files"), dict):
            raise RuntimeError("Invalid or unsupported analysis runtime manifest")
        expected = content["files"]
        if actual != expected:
            raise RuntimeError("Runtime files differ from the staging manifest; restage the runtime")
    print(execute(python, VALIDATE, arguments=(root,)))


def stage(python, destination):
    info = json.loads(execute(python, PROBE, isolated=False))
    source = Path(info["prefix"])
    if not (source / "python.exe").is_file():
        raise RuntimeError("Automatic staging requires a Windows CPython distribution; Unix packagers must supply a relocatable pre-provisioned runtime")
    if destination.exists():
        raise RuntimeError("Destination already exists; choose a new directory (nothing was overwritten)")
    if destination.resolve().is_relative_to(source.resolve()):
        raise RuntimeError("Destination must be outside the source Python installation")
    destination.mkdir(parents=True)
    ignore = shutil.ignore_patterns("site-packages", "__pycache__", "*.pyc", "test", "tests", "idlelib", "tkinter", "turtledemo")
    shutil.copytree(source / "Lib", destination / "Lib", ignore=ignore)
    shutil.copytree(source / "DLLs", destination / "DLLs", ignore=shutil.ignore_patterns("__pycache__"))
    for pattern in ("python*.exe", "*.dll", "LICENSE*", "*.txt", "python*.zip"):
        for path in source.glob(pattern):
            if path.is_file():
                shutil.copy2(path, destination / path.name)
    for package in info["packages"].values():
        package_root = Path(package.pop("root"))
        for name in package.pop("files"):
            path = Path(name)
            if path.is_file() and path.is_relative_to(package_root) and "__pycache__" not in path.parts and path.suffix != ".pyc":
                target = destination / "Lib/site-packages" / path.relative_to(package_root)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, target)
    (destination / "python._pth").write_text(".\nDLLs\nLib\nLib/site-packages\n", encoding="utf-8")
    manifest = {"version": 1, "python": info["version"], "packages": info["packages"], "files": hashes(destination)}
    (destination / "analysis-runtime-manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    validate(destination)
    print(f"Staged {len(manifest['files'])} files in {destination}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--python", type=Path, help="Pre-provisioned Windows CPython interpreter")
    parser.add_argument("--destination", type=Path, help="New isolated runtime directory")
    parser.add_argument("--validate-runtime", type=Path, help="Validate a pre-provisioned runtime without modifying it")
    args = parser.parse_args()
    try:
        if args.validate_runtime:
            validate(args.validate_runtime.resolve())
        elif args.python and args.destination:
            stage(args.python.resolve(), args.destination.resolve())
        else:
            parser.error("Supply --validate-runtime, or both --python and --destination")
    except (OSError, RuntimeError, subprocess.TimeoutExpired, ValueError) as error:
        parser.exit(1, f"Analysis runtime error: {error}\n")


if __name__ == "__main__":
    main()
