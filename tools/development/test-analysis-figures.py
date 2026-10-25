"""Run Windows installed analysis-figure checks after compilation has stopped."""

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--install", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--native", action="store_true")
    parser.add_argument("--gui-only", action="store_true", help="Rerun GUI checks after a GUI-only correction")
    parser.add_argument("--installed-only", action="store_true", help="Exclude developer SDK/toolchain paths; this is not a clean-OS test")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("This runner targets the Windows development candidate; use CTest on other platforms")
    repo = Path(__file__).resolve().parents[2]
    build, install, output = (p.resolve() for p in (args.build, args.install, args.output))
    if output.exists():
        parser.error("Output directory already exists; choose a new evidence directory")
    import msvcrt
    build_lock = (build / "logs/build.lock").open("a+b")
    try:
        build_lock.seek(0)
        msvcrt.locking(build_lock.fileno(), msvcrt.LK_NBLCK, 1)
    except OSError:
        build_lock.close()
        parser.error("Another development build or validation holds the candidate build lock")
    output.mkdir(parents=True)
    env = os.environ.copy()
    sdk = repo.parent / "qt-autocomplete-sdk/ucrt64/bin"
    toolchain = repo.parent / "build-tools/omdev128/tools/msys"
    windows = Path(env.get("SystemRoot", "C:/Windows"))
    env["PATH"] = os.pathsep.join(map(str, [install / "bin", sdk, toolchain / "ucrt64/bin",
                                           toolchain / "usr/bin", windows / "System32", windows]))
    if args.installed_only:
        env["PATH"] = os.pathsep.join(map(str, [install / "bin", windows / "System32", windows]))
    for key in ("OMEDIT_ANALYSIS_PYTHON", "PYTHONHOME", "PYTHONPATH", "QTDIR", "QT_PLUGIN_PATH",
                "QT_QPA_PLATFORM_PLUGIN_PATH", "QML2_IMPORT_PATH", "QML_IMPORT_PATH", "OMC", "OMC_PATH",
                "QTWEBENGINE_RESOURCES_PATH", "QTWEBENGINE_LOCALES_PATH", "QTWEBENGINE_CHROMIUM_FLAGS"):
        env.pop(key, None)
    env.update(OPENMODELICAHOME=str(install), OMDEV=str(repo.parent / "build-tools/omdev128"),
               QT_QPA_PLATFORM="offscreen", QTWEBENGINE_DISABLE_SANDBOX="1", QT_FORCE_STDERR_LOGGING="1",
               QTWEBENGINEPROCESS_PATH=str(install / "bin/QtWebEngineProcess.exe"),
               OMEDIT_ANALYSIS_SMOKE_OUTPUT=str(output / "gui-offscreen"),
               MPLCONFIGDIR=str(output / "matplotlib-cache"), PYTHONDONTWRITEBYTECODE="1")
    if args.installed_only:
        for key in ("OMDEV", "MSYSTEM", "MSYSTEM_PREFIX", "PYTHONUSERBASE", "CMAKE_PREFIX_PATH"):
            env.pop(key, None)
    runtime = install / "share/omedit/analysis/runtime/python.exe"
    tests = build / "OMEdit/Testsuite/AnalysisFigures"
    report = {"started_utc": datetime.now(timezone.utc).isoformat(), "status": "failed", "checks": [],
              "installed_only": args.installed_only, "install": str(install), "path": env["PATH"],
              "clean_os_test": False}
    checks = [
        ("data", [str(tests / "AnalysisFigureDataTest.exe")], 90, {}),
        ("renderer", [str(runtime), "-m", "unittest", "discover", "-s",
                      str(repo / "OMEdit/Resources/analysis"), "-p", "test_*.py"], 120, {}),
        ("gui-offscreen", [str(tests / "AnalysisFigureSmoke.exe")], 120,
         {"QTWEBENGINE_CHROMIUM_FLAGS": "--disable-gpu"}),
    ]
    if args.gui_only:
        checks = checks[2:]
    if args.native:
        checks.append(("gui-native", [str(tests / "AnalysisFigureSmoke.exe")], 120,
                       {"QT_QPA_PLATFORM": "windows", "OMEDIT_ANALYSIS_SMOKE_OUTPUT": str(output / "gui-native")}))
    try:
        for name, command, timeout, overrides in checks:
            started = time.monotonic()
            print(f"Running {name}", flush=True)
            with (output / f"{name}.log").open("w", encoding="utf-8") as stream:
                process = subprocess.Popen(command, cwd=repo, env=env | overrides,
                    stdout=stream, stderr=subprocess.STDOUT,
                    creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
                try:
                    code = process.wait(timeout=timeout)
                except (subprocess.TimeoutExpired, KeyboardInterrupt):
                    if os.name == "nt":
                        subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                                       capture_output=True, timeout=20)
                    else:
                        process.kill()
                    process.wait(timeout=20)
                    raise
            report["checks"].append({"name": name, "exit_code": code,
                                      "seconds": round(time.monotonic() - started, 3)})
            if code:
                print((output / f"{name}.log").read_text(encoding="utf-8", errors="replace")[-12000:])
                raise RuntimeError(f"{name} failed with exit code {code}")
        report["status"] = "passed"
    finally:
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        build_lock.close()
    print(f"Analysis checks passed: {output}")


if __name__ == "__main__":
    main()
