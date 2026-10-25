#!/usr/bin/env python3
"""Validate an installed analysis runtime/worker with only host stdlib dependencies.

An isolated environment on a developer machine is not a clean operating-system
image. Run this same command on each supported clean OS before distribution.
"""
import argparse
from datetime import datetime, timezone
import hashlib
from http.cookiejar import CookieJar
import json
import os
from pathlib import Path
import platform
import queue
import secrets
import struct
import subprocess
import sys
import tempfile
import threading
import time
from urllib.error import HTTPError
from urllib.parse import parse_qs, urljoin, urlsplit
from urllib.request import HTTPCookieProcessor, ProxyHandler, Request, build_opener

PROBE = r'''
import json, pathlib, sys
import matplotlib, numpy, tornado, tornado.websocket
from matplotlib.backends import backend_webagg
root = pathlib.Path(sys.argv[1]).resolve()
modules = {m.__name__: str(pathlib.Path(m.__file__).resolve())
           for m in (matplotlib, numpy, tornado, backend_webagg)}
def inside(path):
    try: pathlib.Path(path).resolve().relative_to(root); return True
    except ValueError: return False
assert all(inside(p) for p in modules.values()), modules
assert all(inside(p) for p in sys.path if p), sys.path
assert sys.flags.isolated and sys.flags.no_user_site, "Python isolation was not enabled"
print(json.dumps({"python": sys.version, "executable": sys.executable,
 "prefix": sys.prefix, "basePrefix": sys.base_prefix, "sysPath": sys.path,
 "isolated": sys.flags.isolated, "noUserSite": sys.flags.no_user_site,
 "modules": modules, "versions": {"matplotlib": matplotlib.__version__,
 "numpy": numpy.__version__, "tornado": tornado.version}}))
'''


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def environment(runtime, sandbox):
    env = {key: os.environ[key] for key in ("SystemRoot", "SYSTEMROOT", "WINDIR", "SystemDrive", "OS") if key in os.environ}
    paths = [str(runtime), str(runtime / "DLLs")] if os.name == "nt" else [str(runtime / "bin")]
    if os.name == "nt":
        paths.append(str(Path(os.environ.get("SystemRoot", "C:/Windows")) / "System32"))
    env.update(PATH=os.pathsep.join(paths), HOME=str(sandbox), USERPROFILE=str(sandbox),
               APPDATA=str(sandbox / "appdata"), LOCALAPPDATA=str(sandbox / "localappdata"),
               XDG_CONFIG_HOME=str(sandbox / "config"), XDG_CACHE_HOME=str(sandbox / "cache"),
               MPLCONFIGDIR=str(sandbox / "matplotlib"), TEMP=str(sandbox), TMP=str(sandbox),
               TMPDIR=str(sandbox), PYTHONNOUSERSITE="1", PYTHONDONTWRITEBYTECODE="1",
               MPLBACKEND="Agg", LANG="C.UTF-8", LC_ALL="C.UTF-8")
    return env


class Worker:
    def __init__(self, command, env, cwd, stderr, timeout):
        self.timeout = timeout
        self.messages = queue.Queue()
        self.sequence = 0
        self.process = subprocess.Popen(command, env=env, cwd=cwd, stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=stderr,
                                        text=True, encoding="utf-8", bufsize=1)
        def read():
            try:
                for line in self.process.stdout:
                    self.messages.put(line)
            finally:
                self.messages.put(None)
        self.reader = threading.Thread(target=read, daemon=True)
        self.reader.start()

    def receive(self):
        try:
            line = self.messages.get(timeout=self.timeout)
        except queue.Empty:
            raise RuntimeError("Worker response timeout") from None
        require(line is not None, "Worker stdout ended before the expected response")
        require(len(line) <= 1_000_000, "Worker response exceeds the validation limit")
        return json.loads(line)

    def request(self, operation, *, close_input=False, **fields):
        self.sequence += 1
        request = dict(id=self.sequence, op=operation, **fields)
        self.process.stdin.write(json.dumps(request, allow_nan=False) + "\n")
        self.process.stdin.flush()
        if close_input:
            self.process.stdin.close()
        response = self.receive()
        require(response.get("id") == self.sequence, "Worker response ID mismatch")
        require(response.get("ok") is True, f"Worker {operation} failed: {response.get('error')}")
        return response

    def cleanup(self):
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)
        self.reader.join(timeout=1)
        for stream in (self.process.stdin, self.process.stdout):
            if stream:
                stream.close()


def payload():
    series = dict(x=[0, .5, .5, 1], y=[1, 2, 3, 2], z=[2, 1, 2, 3],
                  time=[0, .5, .5, 1], label="Deployment signal", xUnit="s")
    return dict(version=1, title="Installed runtime validation", rows=1, columns=3,
                width=12, height=4, dpi=100, fontSize=10, background="white", time=.5,
                panels=[dict(id="line", type="time", title="2D line", series=[series]),
                        dict(id="trajectory", type="trajectory3d", title="3D trajectory", series=[series]),
                        dict(id="surface", type="surface", title="3D surface", surface=dict(
                            x=[1, 2], y=[1, 2], z=[[1, 3], [2, 4]]))])


def http_checks(url, token, timeout):
    parsed = urlsplit(url)
    require(parsed.scheme == "http" and parsed.hostname == "127.0.0.1" and parsed.port,
            "Worker did not advertise a loopback-only HTTP URL")
    require(parse_qs(parsed.query).get("token") == [token], "Startup token mismatch")
    base = f"http://127.0.0.1:{parsed.port}/"
    anonymous = build_opener(ProxyHandler({}))
    for address, headers in ((base, {}), (base + "?token=incorrect", {}),
                             (url, {"Host": "untrusted.example"})):
        try:
            with anonymous.open(Request(address, headers=headers), timeout=timeout):
                raise RuntimeError("Unauthenticated or hostile-Host HTTP request was accepted")
        except HTTPError as error:
            require(error.code == 403, f"Expected HTTP 403, received {error.code}")
    opener = build_opener(ProxyHandler({}), HTTPCookieProcessor(CookieJar()))
    with opener.open(url, timeout=timeout) as response:
        html = response.read(1_000_000)
        require(response.status == 200 and b"mpl.figure" in html, "Authenticated WebAgg page missing")
    assets = {}
    for path in ("mpl.js", "_static/css/mpl.css"):
        with opener.open(urljoin(base, path), timeout=timeout) as response:
            content = response.read(4_000_000)
            require(response.status == 200 and content, f"WebAgg asset unavailable: {path}")
            assets[path] = len(content)
    return {"host": "127.0.0.1", "authentication": "token and cookie accepted; absent/bad token and hostile Host rejected",
            "assets": assets}


def validate(args, report):
    runtime = args.runtime.resolve()
    worker_path = (args.worker or runtime.parent / "analysis_worker.py").resolve()
    interpreter = runtime / ("python.exe" if os.name == "nt" else "bin/python3")
    require(interpreter.is_file(), f"Runtime interpreter missing: {interpreter}")
    require(worker_path.is_file(), f"Installed worker missing: {worker_path}")
    report.update(runtime=str(runtime), worker=str(worker_path), workerSha256=digest(worker_path))
    manifest = runtime / "analysis-runtime-manifest.json"
    if manifest.is_file():
        report["runtimeManifestSha256"] = digest(manifest)
    with tempfile.TemporaryDirectory(prefix="omedit-analysis-deployment-") as directory:
        sandbox = Path(directory)
        env = environment(runtime, sandbox)
        report["environment"] = {"PATH": env["PATH"], "home": "fresh temporary directory",
                                  "pythonEnvironment": "cleared; -I isolated mode",
                                  "nativeLoaderEnvironment": "LD_LIBRARY_PATH, DYLD_* and developer PATH removed"}
        result = subprocess.run([str(interpreter), "-I", "-B", "-c", PROBE, str(runtime)],
                                env=env, cwd=sandbox, capture_output=True, text=True,
                                encoding="utf-8", timeout=args.timeout)
        require(result.returncode == 0, "Runtime origin/import check failed: " + result.stderr)
        report["checks"]["runtimeOrigin"] = json.loads(result.stdout)
        token = secrets.token_urlsafe(32)
        process = None
        with (args.output / "worker-stderr.log").open("w", encoding="utf-8") as diagnostics:
            try:
                process = Worker([str(interpreter), "-I", "-B", str(worker_path), "--token", token, "--port", "0"],
                                 env, sandbox, diagnostics, args.timeout)
                ready = process.receive()
                require(ready.get("event") == "ready", "Worker did not emit the ready event")
                report["checks"]["localHttp"] = http_checks(ready["url"], token, args.timeout)
                rendered = process.request("render", figure=payload())
                require(not rendered.get("errors"), "Mixed 2D/3D render reported errors")
                report["checks"]["render"] = ["time", "trajectory3d", "surface"]
                exports = {}
                for fmt in ("png", "svg", "pdf"):
                    path = (args.output / f"deployment-figure.{fmt}").resolve()
                    process.request("export", format=fmt, path=str(path))
                    content = path.read_bytes()
                    require(len(content) > 100, f"Empty {fmt} export")
                    if fmt == "png":
                        require(content.startswith(b"\x89PNG\r\n\x1a\n"), "Invalid PNG signature")
                        require(struct.unpack(">II", content[16:24]) == (1200, 400), "PNG export size mismatch")
                    elif fmt == "pdf":
                        require(content.startswith(b"%PDF-"), "Invalid PDF signature")
                    else:
                        require(b"<svg" in content[:1000] and b"</svg>" in content[-100:], "Invalid SVG document")
                    exports[fmt] = dict(bytes=len(content), sha256=digest(path))
                report["checks"]["exports"] = exports
                process.request("shutdown", close_input=True)
                require(process.process.wait(timeout=min(args.timeout, 10)) == 0, "Worker shutdown failed")
                report["checks"]["shutdown"] = "clean exit after protocol request plus immediate stdin EOF"
            finally:
                if process:
                    process.cleanup()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, required=True, help="Installed runtime directory")
    parser.add_argument("--worker", type=Path, help="Installed analysis_worker.py (defaults to runtime's parent)")
    parser.add_argument("--output", type=Path, required=True, help="New report/artifact directory; must not exist")
    parser.add_argument("--timeout", type=int, default=30, help="Per-operation timeout in seconds (1-120)")
    args = parser.parse_args()
    if not 1 <= args.timeout <= 120:
        parser.error("--timeout must be between 1 and 120")
    args.output = args.output.resolve()
    if args.output.exists():
        parser.error("--output already exists; choose a fresh directory")
    args.output.mkdir(parents=True)
    report = dict(version=1, startedUtc=datetime.now(timezone.utc).isoformat(),
                  platform=platform.platform(), machine=platform.machine(), hostPython=sys.version,
                  caveat="Environment isolation is not a clean OS or VM. This result does not establish clean-machine deployment or untested platform support.",
                  checks={}, ok=False)
    started = time.monotonic()
    try:
        validate(args, report)
        report["ok"] = True
    except Exception as error:
        report["error"] = f"{type(error).__name__}: {error}"
    report["elapsedSeconds"] = round(time.monotonic() - started, 3)
    path = args.output / "report.json"
    path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps({"ok": report["ok"], "report": str(path), "error": report.get("error")}))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
