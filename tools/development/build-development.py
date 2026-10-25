"""Build the pinned checkout with workspace-local output and bounded subprocesses."""
import argparse
import datetime
import hashlib
import json
import msvcrt
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage", choices=("core", "gui", "build"), default="core")
    parser.add_argument("--jobs", type=int, choices=(1, 2), default=1)
    parser.add_argument("--timeout", type=int, default=21600)
    parser.add_argument("--target", nargs="+", default=["install"])
    parser.add_argument("--initialize", action="store_true", help="Initialize only required pinned submodules")
    parser.add_argument("--cargo-home", type=Path)
    parser.add_argument("--rustup-home", type=Path)
    parser.add_argument("--msys")
    parser.add_argument("--qt")
    parser.add_argument("--java")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    build_tools = repo.parent / "build-tools"
    args.msys = args.msys or str(build_tools / "omdev128/tools/msys")
    args.qt = args.qt or str(repo.parent / "qt-autocomplete-sdk/ucrt64")
    args.java = args.java or str(build_tools / "jre21/bin/java.exe")
    build = repo / "build_cmake/development-128"
    prefix = build / "install_cmake"
    for path in (build, prefix):
        if path.exists() and path.resolve() != path:
            raise SystemExit(f"Refusing redirected candidate path: {path}")
    logs = build / "logs"
    logs.mkdir(parents=True, exist_ok=True)
    build_lock = (logs / "build.lock").open("a+b")
    build_lock.seek(0)
    try:
        msvcrt.locking(build_lock.fileno(), msvcrt.LK_NBLCK, 1)
    except OSError:
        raise SystemExit("Another development build or validation holds the candidate build lock")
    msys = Path(args.msys)
    env = os.environ.copy()
    for variable in ("CC", "CXX", "FC", "F77", "F90", "CFLAGS", "CXXFLAGS", "FFLAGS",
                     "LDFLAGS", "CMAKE_TOOLCHAIN_FILE", "CMAKE_PREFIX_PATH", "CMAKE_GENERATOR",
                     "CMAKE_GENERATOR_PLATFORM", "CMAKE_GENERATOR_TOOLSET", "PKG_CONFIG_PATH",
                     "PKG_CONFIG_LIBDIR", "RUSTFLAGS", "CARGO_ENCODED_RUSTFLAGS"):
        env.pop(variable, None)
    env.update(MSYSTEM="UCRT64", MSYSTEM_PREFIX=(msys / "ucrt64").as_posix(),
               OMDEV=msys.parents[1].as_posix(),
               CARGO_HOME=str(args.cargo_home or build_tools / "cargo"),
               RUSTUP_HOME=str(args.rustup_home or build_tools / "rustup"),
               RUSTUP_TOOLCHAIN="stable-x86_64-pc-windows-gnu",
               CARGO_BUILD_JOBS=str(args.jobs),
               CCACHE_DIR=(build / "ccache").as_posix(), CCACHE_MAXSIZE="2G")
    env["PATH"] = os.pathsep.join([str(Path(args.qt) / "bin"),
        env["CARGO_HOME"] + "/bin", str(msys / "ucrt64/bin"),
        str(msys / "usr/bin"), env["PATH"]])
    cmake = str(msys / "ucrt64/bin/cmake.exe")
    compilers = {"C": msys / "ucrt64/bin/cc.exe",
                 "CXX": msys / "ucrt64/bin/c++.exe",
                 "Fortran": msys / "ucrt64/bin/gfortran.exe"}
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")

    def run(command, name):
        log = logs / f"{stamp}-{name}.log"
        print(f"Running {command!r}\nLog: {log}", flush=True)
        with log.open("w", encoding="utf-8") as stream:
            process = subprocess.Popen(command, cwd=repo, env=env, stdout=stream,
                stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
            try:
                code = process.wait(timeout=args.timeout)
            except (subprocess.TimeoutExpired, KeyboardInterrupt) as error:
                subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                               capture_output=True, timeout=30)
                process.wait(timeout=30)
                reason = "Interrupted" if isinstance(error, KeyboardInterrupt) else "Timed out"
                raise SystemExit(f"{reason}; process tree stopped. See {log}")
        if code:
            print(log.read_text(encoding="utf-8", errors="replace")[-12000:])
            raise SystemExit(code)

    provenance = {"head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo, text=True, timeout=30).strip(),
                  "stage": args.stage, "jobs": args.jobs, "msys": str(msys),
                  "qt": args.qt, "java": args.java, "prefix": str(prefix),
                  "temporary_external_toolchain": not msys.is_relative_to(repo.parents[0] / "build-tools")}
    provenance_path = logs / f"{stamp}-provenance.json"
    provenance["tool_versions"] = {}
    for name, command in {**{key: [str(value), "--version"] for key, value in compilers.items()},
                          "cmake": [cmake, "--version"], "cargo": [env["CARGO_HOME"] + "/bin/cargo.exe", "-vV"]}.items():
        provenance["tool_versions"][name] = subprocess.check_output(command, env=env,
            text=True, stderr=subprocess.STDOUT, timeout=30, creationflags=subprocess.CREATE_NO_WINDOW).strip()
    provenance_path.write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")
    if args.initialize:
        modules = {
            "OMCompiler/3rdParty": "OMCompiler-3rdParty",
            "OMCompiler/Compiler/boot/bomc": "OMBootstrapping",
            "OMEdit/OMEditLIB/Resources/crml-diagram": "CRML-diagram",
        }
        for path, upstream in modules.items():
            run(["git", "config", f"submodule.{path}.url",
                 f"https://github.com/OpenModelica/{upstream}.git"], "submodule-url-" + upstream)
        run(["git", "submodule", "update", "--init", "--depth", "1", *modules], "submodules")
        third = "OMCompiler/3rdParty"
        run(["git", "-C", third, "submodule", "update", "--init", "--depth", "1",
             "SuiteSparse", "sundials", "ColPack-src", "moo", "fmi-library"], "third-party-submodules")
        run(["git", "-C", third + "/moo", "submodule", "update", "--init", "--depth", "1",
             "third-party/Ipopt.cmake", "third-party/fmt"], "moo-submodules")
        run(["git", "-C", third + "/moo/third-party/Ipopt.cmake", "submodule", "update",
             "--init", "--depth", "1", "Ipopt/sources"], "ipopt-submodule")
    if args.stage != "build":
        command = [cmake, "-S", str(repo), "-B", str(build), "-G", "MSYS Makefiles",
            "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_INSTALL_PREFIX={prefix.as_posix()}",
            *[f"-DCMAKE_{language}_COMPILER={path.as_posix()}" for language, path in compilers.items()],
            f"-DCMAKE_MAKE_PROGRAM={(msys / 'usr/bin/make.exe').as_posix()}",
            "-DCMAKE_COLOR_MAKEFILE=OFF", "-DOM_ENABLE_OMSIMULATOR=OFF",
            "-DOM_ENABLE_OMSHELL=OFF", "-DOM_ENABLE_OMNOTEBOOK=OFF", "-DOM_ENABLE_OMSENS_QT=OFF",
            "-DOM_OMC_ENABLE_CPP_RUNTIME=OFF", "-DOM_OMC_ENABLE_PARMODELICA=OFF",
            "-DOM_ENABLE_TESTSUITE=OFF", "-DOM_OMC_ENABLE_RUST=OFF",
            f"-DJava_JAVA_EXECUTABLE={args.java}",
            f"-DCMAKE_PREFIX_PATH={Path(args.qt).as_posix()}",
            f"-DOM_ENABLE_GUI_CLIENTS={'ON' if args.stage == 'gui' else 'OFF'}"]
        if args.stage == "gui":
            command += ["-DOM_OMEDIT_DEVELOPMENT_PROFILE=ON", "-DOM_OMEDIT_ENABLE_TESTS=ON",
                        "-DOM_OMEDIT_DEVELOPMENT_TESTS_ONLY=ON",
                        "-DOM_OMEDIT_ENABLE_ANIMATION=OFF",
                        f"-DOM_DEVELOPMENT_QT_ROOT={Path(args.qt).as_posix()}",
                        f"-DOM_QT_DEPLOYMENT_PREFIX={Path(args.qt).as_posix()}",
                        f"-DQt6WebEngineCore_DIR={repo.as_posix()}/tools/development/cmake/Qt6WebEngineCore",
                        f"-DQt6WebEngineWidgets_DIR={repo.as_posix()}/tools/development/cmake/Qt6WebEngineWidgets"]
        run(command, f"configure-{args.stage}")
    provenance["cmake_cache"] = {}
    cache_keys = {"CMAKE_C_COMPILER", "CMAKE_CXX_COMPILER", "CMAKE_Fortran_COMPILER",
                  "CMAKE_MAKE_PROGRAM", "CMAKE_INSTALL_PREFIX", "CMAKE_BUILD_TYPE",
                  "CARGO_EXECUTABLE", "COMPILER_CACHE_PROGRAM", "Java_JAVA_EXECUTABLE",
                  "SOURCE_REVISION", "OM_OMC_ENABLE_RUST", "OM_ENABLE_GUI_CLIENTS",
                  "OM_OMEDIT_DEVELOPMENT_PROFILE", "CMAKE_PREFIX_PATH", "OM_DEVELOPMENT_QT_ROOT"}
    for line in (build / "CMakeCache.txt").read_text(encoding="utf-8").splitlines():
        if not line or line.startswith(("#", "//")) or "=" not in line or ":" not in line:
            continue
        key_type, value = line.split("=", 1)
        key = key_type.split(":", 1)[0]
        if key in cache_keys:
            provenance["cmake_cache"][key] = value
    provenance_path.write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")
    actual_prefix = Path(provenance["cmake_cache"]["CMAKE_INSTALL_PREFIX"])
    if actual_prefix.resolve() != prefix:
        raise SystemExit(f"Refusing build with a different installation prefix: {actual_prefix}")
    for language, intended in compilers.items():
        selected = Path(provenance["cmake_cache"][f"CMAKE_{language}_COMPILER"])
        if selected.resolve() != intended.resolve():
            raise SystemExit(f"Cached {language} compiler differs; configure the desired toolchain first: {selected}")
    for index, target in enumerate(args.target, 1):
        run([cmake, "--build", str(build), "--target", target,
             "--parallel", str(args.jobs)], "build" if len(args.target) == 1 else f"build-{index}")
    if "install" in args.target:
        manifest = {"schema_version": 1, "completed_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                    "build": str(build), "prefix": str(prefix), "head": provenance["head"],
                    "msys": str(msys), "java": provenance["cmake_cache"].get("Java_JAVA_EXECUTABLE", args.java),
                    "qt": provenance["cmake_cache"].get("OM_DEVELOPMENT_QT_ROOT", args.qt),
                    "development_profile": provenance["cmake_cache"].get("OM_OMEDIT_DEVELOPMENT_PROFILE") == "ON",
                    "gui_enabled": provenance["cmake_cache"].get("OM_ENABLE_GUI_CLIENTS") == "ON",
                    "cmake_cache": provenance["cmake_cache"], "artifacts": {}}
        for name in ("omc", "OMEdit"):
            artifact = prefix / "bin" / f"{name}.exe"
            if artifact.is_file():
                with artifact.open("rb") as stream:
                    manifest["artifacts"][name] = {"path": str(artifact),
                        "sha256": hashlib.file_digest(stream, "sha256").hexdigest()}
        if "omc" not in manifest["artifacts"]:
            raise SystemExit("Install succeeded but candidate omc.exe is absent")
        if manifest["gui_enabled"] and "OMEdit" not in manifest["artifacts"]:
            raise SystemExit("GUI install succeeded but candidate OMEdit.exe is absent")
        pending = build / "installed-development.json.tmp"
        pending.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        pending.replace(build / "installed-development.json")


if __name__ == "__main__":
    main()
