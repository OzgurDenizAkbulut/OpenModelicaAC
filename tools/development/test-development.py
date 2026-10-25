#!/usr/bin/env python3
"""Validate an explicitly selected, installed development build. Never builds it."""

import argparse
import ctypes
from ctypes import wintypes
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time
import zipfile


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def modules(pid):
    if os.name != "nt":
        return [], None
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.K32EnumProcessModules.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.HMODULE), wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]
    kernel.K32GetModuleFileNameExW.argtypes = [wintypes.HANDLE, wintypes.HMODULE, wintypes.LPWSTR, wintypes.DWORD]
    handle = kernel.OpenProcess(0x410, False, pid)
    if not handle:
        return [], "OpenProcess: " + str(ctypes.get_last_error())
    try:
        handles = (wintypes.HMODULE * 2048)()
        needed = wintypes.DWORD()
        if not kernel.K32EnumProcessModules(handle, handles, ctypes.sizeof(handles), ctypes.byref(needed)):
            return [], "K32EnumProcessModules: " + str(ctypes.get_last_error())
        paths = []
        for module in handles[:min(len(handles), needed.value // ctypes.sizeof(wintypes.HMODULE))]:
            path = ctypes.create_unicode_buffer(32768)
            if kernel.K32GetModuleFileNameExW(handle, module, path, len(path)):
                paths.append(path.value)
        return paths, None
    finally:
        kernel.CloseHandle(handle)


def main():
    require(os.name != "nt" or sys.version_info >= (3, 12), "Windows acceptance requires Python 3.12+ for junction auditing")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--toolchain-bin", type=Path, action="append", default=[])
    parser.add_argument("--omdev", type=Path, help="Explicit compiler toolchain root containing tools/msys")
    parser.add_argument("--modelica-library-path", type=Path, help="Read-only existing package directory for a pinned Modelica 4.1.0 smoke")
    parser.add_argument("--output", type=Path, required=True, help="Fresh evidence directory")
    parser.add_argument("--expected-version", default="1.28.0-dev")
    parser.add_argument("--expected-revision", default="40d9aae9b8")
    parser.add_argument("--compiler-only", action="store_true")
    parser.add_argument("--native-gui", action="store_true", help="Also run the full-app smoke with the native Windows Qt platform")
    parser.add_argument("--normal-startup", action="store_true", help="Also probe normal installed Windows OMEdit through the actual launcher")
    parser.add_argument("--timeout", type=int, default=300)
    args = parser.parse_args()
    install, build, output = args.install.resolve(), args.build.resolve(), args.output.resolve()
    require(not output.exists(), "Use a fresh evidence directory; existing results are never overwritten")
    require(build.is_dir(), "Selected candidate build directory is missing")
    logs = build / "logs"
    logs.mkdir(exist_ok=True)
    build_lock = (logs / "build.lock").open("a+b")
    build_lock.seek(0)
    try:
        if os.name == "nt":
            import msvcrt
            msvcrt.locking(build_lock.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            import fcntl
            fcntl.flock(build_lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        build_lock.close()
        raise RuntimeError("Another development build or validation holds the candidate build lock")
    output.mkdir(parents=True)
    acceptance_started = time.monotonic()
    report = {"install": str(install), "build": str(build), "python": sys.version, "processes": [], "status": "failed"}
    report["started_utc"] = datetime.now(timezone.utc).isoformat()
    env = os.environ.copy()
    for key in list(env):
        if key.startswith("OMEDIT_SMOKE_") or key == "OMEDIT_INLINE_PREVIEW_SCREENSHOT":
            env.pop(key, None)
    # Keep compiler lookup independent of any official installation on the parent PATH.
    system_root = Path(os.environ.get("SystemRoot", "/usr"))
    toolchain_dirs = [p.resolve() for p in args.toolchain_bin]
    path_dirs = [install / "bin", *toolchain_dirs]
    path_dirs += [system_root / "System32", system_root] if os.name == "nt" else [Path("/usr/bin"), Path("/bin")]
    env["PATH"] = os.pathsep.join(map(str, path_dirs))
    env["OPENMODELICAHOME"] = str(install)
    env["OPENMODELICALIBRARY"] = str(output / "libraries")
    env.pop("OMDEV", None)
    omdev = args.omdev.resolve() if args.omdev else None
    if omdev is None:
        for directory in toolchain_dirs:
            if directory.parts[-4:] == ("tools", "msys", "ucrt64", "bin"):
                omdev = directory.parents[3]
                break
    if omdev:
        require((omdev / "tools/msys/ucrt64/bin").is_dir(), "OMDEV lacks tools/msys/ucrt64/bin")
        env["OMDEV"] = str(omdev)
    for key in ("HOME", "USERPROFILE", "APPDATA", "LOCALAPPDATA", "XDG_CONFIG_HOME", "XDG_CACHE_HOME", "TEMP", "TMP"):
        directory = output / "isolated-home" / key.lower()
        directory.mkdir(parents=True)
        env[key] = str(directory)
    env.update(QT_QPA_PLATFORM="offscreen", QTWEBENGINE_DISABLE_SANDBOX="1", QT_FORCE_STDERR_LOGGING="1",
               OMEDIT_SMOKE_OUTPUT=str(output / "gui"),
               OMEDIT_SMOKE_RESULT=str(output / "DevelopmentAcceptance_res.mat"))
    for key in ("QTDIR", "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QML2_IMPORT_PATH", "QML_IMPORT_PATH", "OMC", "OMC_PATH",
                "QTWEBENGINEPROCESS_PATH", "QTWEBENGINE_CHROMIUM_FLAGS", "QTWEBENGINE_REMOTE_DEBUGGING"):
        env.pop(key, None)
    for key in ("QTWEBENGINE_RESOURCES_PATH", "QTWEBENGINE_LOCALES_PATH", "QTWEBENGINE_DICTIONARIES_PATH"):
        env.pop(key, None)
    if not args.compiler_only and os.name == "nt":
        env["QTWEBENGINEPROCESS_PATH"] = str(install / "bin/QtWebEngineProcess.exe")
    report["environment"] = {key: env[key] for key in ("PATH", "OPENMODELICAHOME", "OPENMODELICALIBRARY", "QT_QPA_PLATFORM", "USERPROFILE")}
    report["environment"]["OMDEV"] = env.get("OMDEV")

    def run(name, command, timeout, overrides=None):
        started = time.monotonic()
        seen = set()
        module_errors = set()
        stdout = output / (name + ".log")
        entry = {"name": name, "command": list(map(str, command)), "log": str(stdout)}
        entry["started_utc"] = datetime.now(timezone.utc).isoformat()
        private_temp = output / "process-temp" / name
        private_temp.mkdir(parents=True)
        process_env = dict(env, **(overrides or {}), TEMP=str(private_temp), TMP=str(private_temp))
        entry["temporary_directory"] = str(private_temp)
        if overrides:
            entry["environment_overrides"] = overrides
        report["processes"].append(entry)
        startup = None
        if os.name == "nt":
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = subprocess.SW_HIDE
        with stdout.open("w", encoding="utf-8") as log:
            process = subprocess.Popen(command, cwd=output, env=process_env, stdout=log, stderr=subprocess.STDOUT,
                                       creationflags=(subprocess.CREATE_NO_WINDOW | subprocess.CREATE_NEW_PROCESS_GROUP) if os.name == "nt" else 0,
                                       startupinfo=startup, start_new_session=os.name != "nt")
            try:
                deadline = time.monotonic() + timeout
                while process.poll() is None:
                    paths, enumeration_error = modules(process.pid)
                    seen.update(paths)
                    if enumeration_error:
                        module_errors.add(enumeration_error)
                    if time.monotonic() >= deadline:
                        raise RuntimeError(name + " timed out")
                    time.sleep(0.05)
            except BaseException:
                previous_sigint = signal.signal(signal.SIGINT, signal.SIG_IGN)
                try:
                    if os.name == "nt":
                        try:
                            cleanup = subprocess.run([str(system_root / "System32/taskkill.exe"), "/PID", str(process.pid), "/T", "/F"],
                                                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                                     creationflags=subprocess.CREATE_NO_WINDOW, timeout=15)
                            entry["tree_cleanup_returncode"] = cleanup.returncode
                        except (OSError, subprocess.TimeoutExpired) as cleanup_error:
                            entry["tree_cleanup_error"] = str(cleanup_error)
                        if process.poll() is None:
                            process.kill()
                    else:
                        try:
                            os.killpg(process.pid, signal.SIGKILL)
                        except ProcessLookupError:
                            pass
                    process.wait(timeout=15)
                    entry["interrupted_process_stopped"] = True
                finally:
                    signal.signal(signal.SIGINT, previous_sigint)
                raise
            finally:
                entry.update(returncode=process.poll(), modules=sorted(seen), module_enumeration_errors=sorted(module_errors),
                             elapsed_seconds=time.monotonic() - started)
        for path in seen:
            require("1.27" not in path.lower() or any(Path(path).resolve().is_relative_to(p) for p in toolchain_dirs),
                    "Loaded legacy DLL outside explicitly disclosed toolchain: " + path)
            if re.search(r"(?:lib)?(?:openmodelicacompiler|openmodelicaruntime|omcruntime|simulationruntimec|omcgc|omc_result|omcresult).*\.dll$", Path(path).name, re.I):
                require(Path(path).resolve().is_relative_to(install), "Compiler/runtime DLL outside candidate: " + path)
            if os.name == "nt" and Path(path).name.lower() == "qt6core.dll":
                require(Path(path).resolve() == (install / "bin/Qt6Core.dll").resolve(), "Qt6Core loaded outside candidate: " + path)
        if os.name == "nt" and (name == "compiler-smoke" or name.startswith("OMEditDevelopmentSmoke")):
            require(any(re.fullmatch(r"(?:lib)?OpenModelicaCompiler\.dll", Path(p).name, re.I) for p in seen),
                    "No positive compiler DLL provenance observed for " + name)
        if os.name == "nt" and name.startswith("OMEditDevelopmentSmoke"):
            require(any(Path(p).name.lower() == "qt6core.dll" for p in seen), "No positive Qt6Core provenance observed")
        require(process.returncode == 0, name + " failed: " + str(stdout))
        contents = stdout.read_text(encoding="utf-8", errors="replace")
        if name.startswith("OMEditDevelopmentSmoke"):
            require("SMOKE PASSED" in contents and "real simulation result plot screenshot saved" in contents
                    and "real model diagram screenshot saved" in contents
                    and "Tab accepts completion through the real editor event filter" in contents,
                    "Full-app smoke did not exercise real diagram, editor completion and candidate-result plotting")
        return contents

    try:
        require(install.is_relative_to(build), "Install must be inside the selected candidate build")
        audited = []
        for root in ("bin", "lib", "include", "share"):
            directory = install / root
            require(directory.exists(), "Missing candidate install directory: " + str(directory))
            require(directory.resolve().is_relative_to(install), "Candidate install directory redirects outside installation: " + str(directory))
            for path in [directory, *directory.rglob("*")]:
                if path.is_symlink() or (hasattr(path, "is_junction") and path.is_junction()):
                    audited.append({"path": str(path), "target": str(path.resolve())})
                    require(path.resolve().is_relative_to(install), "Install link escapes candidate: " + str(path))
        report["links"] = audited
        omc = install / "bin" / ("omc.exe" if os.name == "nt" else "omc")
        require(omc.exists(), "Installed candidate omc missing")
        report["omc_sha256"] = hashlib.sha256(omc.read_bytes()).hexdigest()
        version = run("compiler-version", [str(omc), "--version"], 30)
        require(args.expected_version in version and args.expected_revision in version, "Compiler revision does not match requested source: " + version)
        model = output / "DevelopmentAcceptance.mo"
        model.write_text('''model DevelopmentAcceptance
  Real x(start=1, fixed=true);
equation
  der(x) = -x;
end DevelopmentAcceptance;
model ConditionalAcceptance
  parameter Boolean useX=true;
  Real x if useX;
end ConditionalAcceptance;
''', encoding="utf-8")
        script = output / "compiler-smoke.mos"
        script.write_text('''loadFile("DevelopmentAcceptance.mo"); getErrorString();
print("INSTALLATION=" + getInstallationDirectoryPath() + "\\n"); getErrorString();
checkModel(DevelopmentAcceptance); getErrorString();
writeFile("model-instance.json", getModelInstance(DevelopmentAcceptance)); getErrorString();
print("CONDITION=" + getComponentCondition(ConditionalAcceptance, x) + "\\n"); getErrorString();
simulate(DevelopmentAcceptance, stopTime=1, tolerance=1e-8, numberOfIntervals=10, outputFormat="mat"); getErrorString();
print("VALUE_OK=" + String(abs(val(x, 1, "DevelopmentAcceptance_res.mat") - exp(-1)) < 1e-5) + "\\n"); getErrorString();
print("RESULT_SIZE=" + String(readSimulationResultSize("DevelopmentAcceptance_res.mat")) + "\\n"); getErrorString();
samples := readSimulationResult("DevelopmentAcceptance_res.mat", {x}, 0); getErrorString();
print("SAMPLES_OK=" + String(size(samples, 1) == 1 and size(samples, 2) > 1) + "\\n"); getErrorString();
closeSimulationResultFile(); getErrorString();
buildModelFMU(DevelopmentAcceptance, version="2.0", fmuType="me"); getErrorString();
''', encoding="utf-8")
        result = run("compiler-smoke", [str(omc), str(script)], args.timeout)
        require("completed successfully" in result and "VALUE_OK=true" in result and "CONDITION=useX" in result and "SAMPLES_OK=true" in result,
                "Compiler load/check/simulation/value/component-condition assertions failed")
        home = re.search(r"INSTALLATION=([^\r\n]+)", result)
        require(home and Path(home.group(1)).resolve() == install, "Compiler installation data path escapes candidate")
        require(re.search(r"RESULT_SIZE=([1-9][0-9]*)", result), "Result reader returned no data")
        instance = json.loads((output / "model-instance.json").read_text(encoding="utf-8"))
        require(instance.get("name") == "DevelopmentAcceptance" and instance.get("elements"), "Invalid model-instance JSON")
        fmu = output / "DevelopmentAcceptance.fmu"
        with zipfile.ZipFile(fmu) as archive:
            names = archive.namelist()
            binaries = [info for info in archive.infolist() if info.filename.startswith("binaries/")
                        and Path(info.filename).suffix.lower() in (".dll", ".so", ".dylib") and info.file_size > 0]
            require("modelDescription.xml" in names and binaries, "FMU lacks model description or nonempty platform binary")
        if args.modelica_library_path:
            library_path = args.modelica_library_path.resolve()
            require(library_path.is_dir(), "Explicit Modelica library path is missing")
            msl_model = output / "MSLAcceptance.mo"
            msl_model.write_text('''model MSLAcceptance
  Modelica.Blocks.Sources.Constant source(k=1);
  Modelica.Blocks.Continuous.FirstOrder firstOrder(k=1, T=1,
    initType=Modelica.Blocks.Types.Init.InitialState, y_start=0);
equation
  connect(source.y, firstOrder.u);
end MSLAcceptance;
''', encoding="utf-8")
            msl_script = output / "msl-smoke.mos"
            msl_script.write_text('''print("MSL_LOADED=" + String(loadModel(Modelica, {"4.1.0"})) + "\\n"); getErrorString();
print("MSL_VERSION=" + getVersion(Modelica) + "\\n"); getErrorString();
print("MSL_SOURCE=" + getSourceFile(Modelica) + "\\n"); getErrorString();
loadFile("MSLAcceptance.mo"); getErrorString();
checkModel(MSLAcceptance); getErrorString();
simulate(MSLAcceptance, stopTime=1, tolerance=1e-8, numberOfIntervals=10); getErrorString();
print("MSL_VALUE_OK=" + String(abs(val(firstOrder.y, 1, "MSLAcceptance_res.mat") - (1-exp(-1))) < 1e-5) + "\\n"); getErrorString();
closeSimulationResultFile(); getErrorString();
''', encoding="utf-8")
            msl_result = run("compiler-msl-smoke", [str(omc), str(msl_script)], args.timeout,
                             {"OPENMODELICALIBRARY": str(library_path)})
            require("MSL_LOADED=true" in msl_result and "MSL_VERSION=4.1.0" in msl_result
                    and "completed successfully" in msl_result and "MSL_VALUE_OK=true" in msl_result,
                    "Pinned Modelica 4.1.0 loading/check/simulation failed")
            source_match = re.search(r"MSL_SOURCE=([^\r\n]+)", msl_result)
            require(source_match is not None, "Modelica package source path was not returned")
            package_source = Path(source_match.group(1)).resolve()
            require(package_source.is_relative_to(library_path), "Modelica loaded from outside the explicit package directory")
            report["modelica_library"] = {"path": str(library_path), "version": "4.1.0",
                                         "source": str(package_source),
                                         "package_sha256": hashlib.sha256(package_source.read_bytes()).hexdigest()}
        if not args.compiler_only:
            installed_gui = install / "bin" / ("OMEdit.exe" if os.name == "nt" else "OMEdit")
            require(installed_gui.is_file(), "Installed candidate OMEdit is missing")
            if os.name == "nt":
                require(Path(env["QTWEBENGINEPROCESS_PATH"]).is_file(), "Candidate QtWebEngineProcess.exe is missing")
                qt_core = install / "bin/Qt6Core.dll"
                require(qt_core.is_file(), "Candidate Qt6Core.dll is missing")
                report["qt_core"] = {"path": str(qt_core), "sha256": hashlib.sha256(qt_core.read_bytes()).hexdigest()}
            run("installed-omedit-help", [str(installed_gui), "--help"], 30)
            plugin_roots = [install / "bin", install / "bin/plugins", install / "share/qt6/plugins", install / "plugins"]
            plugin_root = next((p for p in plugin_roots if (p / "platforms").is_dir()), None)
            require(plugin_root is not None, "Candidate Qt platform plugins are missing")
            if args.native_gui:
                require((plugin_root / "platforms/qwindows.dll").is_file(), "Candidate native qwindows.dll platform plugin is missing")
            env["QT_PLUGIN_PATH"] = str(plugin_root)
            env["QT_QPA_PLATFORM_PLUGIN_PATH"] = str(plugin_root / "platforms")
            if os.name == "nt":
                resources = install / "bin/resources"
                locales = install / "bin/share/qt6/translations/qtwebengine_locales"
                require(resources.is_dir() and (locales / "en-US.pak").is_file(), "Candidate WebEngine resources/locales are missing")
                env["QTWEBENGINE_RESOURCES_PATH"] = str(resources)
                env["QTWEBENGINE_LOCALES_PATH"] = str(locales)
            report["gui_environment"] = {key: env[key] for key in ("QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QTWEBENGINEPROCESS_PATH",
                                                                    "QTWEBENGINE_RESOURCES_PATH", "QTWEBENGINE_LOCALES_PATH") if key in env}
            for name in ("LocalCompletionClientTest", "InlineCompletionTest", "OMEditDevelopmentSmoke"):
                matches = list((build / "OMEdit" / "Testsuite").rglob(name + (".exe" if os.name == "nt" else "")))
                require(len(matches) == 1, "Expected one built test executable: " + name)
                test_command = [str(matches[0])]
                if name != "OMEditDevelopmentSmoke":
                    test_command.extend(["-o", str(output / (name + "-results.txt")) + ",txt"])
                run(name, test_command, 80)
                if name == "OMEditDevelopmentSmoke" and args.native_gui:
                    require(os.name == "nt", "--native-gui currently supports Windows only")
                    run(name + "-native", [str(matches[0])], 80,
                        {"QT_QPA_PLATFORM": "windows", "OMEDIT_SMOKE_OUTPUT": str(output / "gui-native")})
            if args.normal_startup:
                require(os.name == "nt", "--normal-startup currently supports Windows only")
                powershell = system_root / "System32/WindowsPowerShell/v1.0/powershell.exe"
                startup_output = output / "normal-startup"
                command = [str(powershell), "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File",
                           str(Path(__file__).with_name("probe-installed-startup.ps1")), "-Install", str(install), "-Output", str(startup_output)]
                source_revision = subprocess.check_output(
                    ["git", "-C", str(Path(__file__).resolve().parents[2]), "rev-parse", "HEAD"],
                    text=True, timeout=15).strip()
                command.extend(["-ExpectedRevision", source_revision])
                if os.environ.get("APPDATA"):
                    command.extend(["-OriginalRoaming", os.environ["APPDATA"]])
                if os.environ.get("USERPROFILE"):
                    command.extend(["-OriginalUserProfile", os.environ["USERPROFILE"]])
                if args.modelica_library_path:
                    command.extend(["-ModelicaLibraryPath", str(args.modelica_library_path.resolve())])
                run("installed-normal-startup", command, 80)
                report["normal_startup"] = json.loads((startup_output / "report.json").read_text(encoding="utf-8-sig"))
                require(report["normal_startup"]["status"] == "passed", "Installed normal-startup probe failed")
        report["status"] = "passed"
        print("Development acceptance passed; evidence: " + str(output))
    except BaseException as error:
        report["error"] = type(error).__name__ + ": " + str(error)
        if isinstance(error, KeyboardInterrupt):
            report["status"] = "interrupted"
        raise
    finally:
        report["completed_utc"] = datetime.now(timezone.utc).isoformat()
        report["elapsed_seconds"] = time.monotonic() - acceptance_started
        (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
        build_lock.close()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("Development acceptance interrupted; see evidence report for process cleanup", file=sys.stderr)
        sys.exit(130)
    except (RuntimeError, OSError, ValueError, zipfile.BadZipFile) as error:
        print("Development acceptance FAILED: " + str(error), file=sys.stderr)
        sys.exit(1)
