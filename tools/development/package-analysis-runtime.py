#!/usr/bin/env python3
"""Package a validated Windows analysis component and portable deployment checks.

Reads an existing runtime; never downloads or installs dependencies. A fresh
output directory contains both an unpacked component and a ZIP for transfer.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import sys
import zipfile


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    runtime, output = args.runtime.resolve(), args.output.resolve()
    if os.name != "nt" or not (runtime / "python.exe").is_file():
        parser.error("This ZIP packager targets Windows; Unix runtimes need native packaging")
    if output.exists() or output.is_relative_to(runtime):
        parser.error("Choose a fresh output directory outside the input runtime")
    if not (runtime / "analysis-runtime-manifest.json").is_file():
        parser.error("The runtime must have a staging manifest")
    tooling = Path(__file__).resolve().parent
    repo = tooling.parents[1]
    spec = importlib.util.spec_from_file_location("analysis_staging", tooling / "stage-analysis-runtime.py")
    staging = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(staging)
    staging.validate(runtime)
    output.mkdir(parents=True)
    component = output / "OMEdit Analysis Component"
    analysis = component / "share/omedit/analysis"
    analysis.mkdir(parents=True)
    shutil.copytree(runtime, analysis / "runtime", ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
    shutil.copy2(repo / "OMEdit/Resources/analysis/analysis_worker.py", analysis / "analysis_worker.py")
    validation = component / "validation"
    validation.mkdir()
    for name in ("stage-analysis-runtime.py", "test-analysis-deployment.py"):
        shutil.copy2(tooling / name, validation / name)
    (component / "README.txt").write_text(
        "OMEdit optional analysis component (Windows)\n\n"
        "Overlay share/omedit/analysis onto an OMEdit installation built with Analysis Figures.\n"
        "This archive contains the plotting runtime and worker, not the OMEdit application.\n"
        "It does not install Python or change user settings.\n\n"
        "To validate on a clean Windows machine, extract the WHOLE archive, open PowerShell\n"
        "in this directory and run (choose fresh evidence directories):\n\n"
        ".\\share\\omedit\\analysis\\runtime\\python.exe -I .\\validation\\stage-analysis-runtime.py "
        "--validate-runtime .\\share\\omedit\\analysis\\runtime\n"
        ".\\share\\omedit\\analysis\\runtime\\python.exe -I .\\validation\\test-analysis-deployment.py "
        "--runtime .\\share\\omedit\\analysis\\runtime --output .\\deployment-evidence\n\n"
        "Return deployment-evidence/report.json and worker-stderr.log. Keep exported figures\n"
        "for visual review. This checks the runtime and worker; separately verify the\n"
        "OMEdit GUI, browser interaction, installation and uninstallation on that OS image.\n"
        "A passing check on a developer workstation is not clean-OS certification.\n",
        encoding="utf-8")
    staging.validate(analysis / "runtime")
    files = {p.relative_to(component).as_posix(): sha256(p)
             for p in sorted(component.rglob("*")) if p.is_file()}
    manifest = {"version": 1, "platform": "windows", "scope": "optional-analysis-component",
                "cleanOsValidated": False, "files": files}
    (component / "component-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    archive = output / "omedit-analysis-windows.zip"
    with zipfile.ZipFile(archive, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as package:
        for path in sorted(component.rglob("*")):
            if path.is_file():
                package.write(path, path.relative_to(output).as_posix())
    (output / "archive-sha256.txt").write_text(f"{sha256(archive)}  {archive.name}\n", encoding="ascii")
    print(json.dumps({"archive": str(archive), "bytes": archive.stat().st_size,
                      "component": str(component), "sha256": sha256(archive)}, indent=2))


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError) as error:
        print(f"Analysis packaging error: {error}", file=sys.stderr)
        sys.exit(1)
