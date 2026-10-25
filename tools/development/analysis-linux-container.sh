#!/usr/bin/env bash
# Run only inside the pinned disposable Linux container, with the documented mounts.
set -euo pipefail
exec > >(tee /out/provision.log) 2>&1
export UV_PYTHON_INSTALL_DIR=/opt/managed-python
export UV_CACHE_DIR=/tmp/uv-cache
export UV_NO_CONFIG=1
export PYTHONDONTWRITEBYTECODE=1
export PYTHONNOUSERSITE=1
export OMEDIT_ANALYSIS_IMAGE='ghcr.io/astral-sh/uv:0.13.5-trixie-slim@sha256:5a9953f569006028f707a8a436609cf5589e140eca41c0ba564dbd7d962c3b46'
uv --version
cat /etc/os-release
ldd --version | head -n 1
uv python install 3.14.3
runtime_candidates=(/opt/managed-python/cpython-3.14.3-*)
[[ ${#runtime_candidates[@]} -eq 1 && -d "${runtime_candidates[0]}" ]]
source_runtime="${runtime_candidates[0]}"
python="$source_runtime/bin/python3"
"$python" -I -B -c 'import sys; print(sys.version); assert sys.version_info[:3] == (3,14,3)'
printf 'matplotlib==3.10.8\nnumpy==2.4.2\ntornado==6.5.4\n' > /out/requirements.in
uv pip compile --python "$python" --generate-hashes --output-file /out/requirements-lock.txt /out/requirements.in > /out/resolve.log 2>&1
uv pip install --python "$python" --system --break-system-packages --require-hashes --only-binary :all: -r /out/requirements-lock.txt
"$python" -I -B - <<'PY'
import importlib.metadata as metadata,json,os,pathlib,platform
out=pathlib.Path('/out')
info={"version":1,"image":os.environ['OMEDIT_ANALYSIS_IMAGE'],"platform":platform.platform(),"machine":platform.machine(),"libc":platform.libc_ver(),"osRelease":pathlib.Path('/etc/os-release').read_text(),"packages":{d.metadata['Name']:d.version for d in metadata.distributions()},"scope":"Linux headless analysis runtime/worker; OMEdit GUI was not tested","cleanOsValidated":False}
(out/'container-provenance.json').write_text(json.dumps(info,indent=2,sort_keys=True)+'\n')
print(json.dumps(info,indent=2,sort_keys=True))
PY
export ANALYSIS_COMPONENT='/opt/OMEdit Analysis Linux Component'
mkdir -p "$ANALYSIS_COMPONENT/share/omedit/analysis" "$ANALYSIS_COMPONENT/validation"
cp -a "$source_runtime" "$ANALYSIS_COMPONENT/share/omedit/analysis/runtime"
cp /analysis-source/analysis_worker.py /analysis-source/requirements.txt "$ANALYSIS_COMPONENT/share/omedit/analysis/"
cp /validation-source/stage-analysis-runtime.py /validation-source/test-analysis-deployment.py "$ANALYSIS_COMPONENT/validation/"
cp /out/requirements.in /out/requirements-lock.txt /out/container-provenance.json "$ANALYSIS_COMPONENT/"
"$python" -I -B - <<'PY'
import importlib.util,json,os,pathlib,sys
component=pathlib.Path(os.environ['ANALYSIS_COMPONENT'])
root=component/'share/omedit/analysis/runtime'
spec=importlib.util.spec_from_file_location('staging','/validation-source/stage-analysis-runtime.py')
staging=importlib.util.module_from_spec(spec);spec.loader.exec_module(staging)
manifest={"version":1,"python":sys.version,"platform":"linux-x86_64","files":staging.hashes(root)}
(root/'analysis-runtime-manifest.json').write_text(json.dumps(manifest,indent=2,sort_keys=True)+'\n')
licenses=[str(p.relative_to(root)) for p in root.rglob('*') if p.is_file() and any(word in p.name.lower() for word in ('license','copying','copyright','notice'))]
assert licenses,'Runtime contains no license documents'
(component/'license-inventory.json').write_text(json.dumps(licenses,indent=2)+'\n')
print(f'Runtime manifest contains {len(manifest["files"])} files; {len(licenses)} license/notice files retained')
PY
relocated_python="$ANALYSIS_COMPONENT/share/omedit/analysis/runtime/bin/python3"
"$relocated_python" -I -B "$ANALYSIS_COMPONENT/validation/stage-analysis-runtime.py" --validate-runtime "$ANALYSIS_COMPONENT/share/omedit/analysis/runtime" | tee /out/staging-validation.log
"$relocated_python" -I -B "$ANALYSIS_COMPONENT/validation/test-analysis-deployment.py" --runtime "$ANALYSIS_COMPONENT/share/omedit/analysis/runtime" --output /out/deployment-relocated
if ! "$relocated_python" -I -B -m unittest discover -s /analysis-source -p 'test_*.py' -v > /out/renderer-tests.log 2>&1; then cat /out/renderer-tests.log; exit 1; fi
if ! "$relocated_python" -I -B /validation-source/test-stage-analysis-runtime.py -v > /out/stage-tests.log 2>&1; then cat /out/stage-tests.log; exit 1; fi
tail -n 4 /out/renderer-tests.log
tail -n 4 /out/stage-tests.log
# Record shared-library resolution for native modules exercised by figure exports.
"$relocated_python" -I -B - <<'PY'
import io,json,pathlib,subprocess,sys
import numpy,tornado,matplotlib
matplotlib.use('Agg')
from matplotlib.figure import Figure
from matplotlib.backends.backend_agg import FigureCanvasAgg
from matplotlib.backends import backend_webagg
fig=Figure();FigureCanvasAgg(fig);fig.add_subplot(projection='3d').plot([0,1],[1,2],[2,3])
for fmt in ('png','svg','pdf'):fig.savefig(io.BytesIO(),format=fmt)
paths=sorted({sys.executable}|{m.__file__ for m in tuple(sys.modules.values()) if str(getattr(m,'__file__','')).endswith('.so')})
results={}
for path in paths:
    check=subprocess.run(['/usr/bin/ldd',path],capture_output=True,text=True,timeout=10)
    results[path]=check.stdout+check.stderr
    assert check.returncode==0 and 'not found' not in results[path],f'Unresolved active native dependency: {path}'
    assert '/opt/managed-python' not in results[path],f'Native dependency uses original runtime: {path}'
pathlib.Path('/out/native-dependencies.json').write_text(json.dumps(results,indent=2,sort_keys=True)+'\n')
print(f'ldd resolved dependencies of {len(paths)} exercised native modules')
PY
cat > "$ANALYSIS_COMPONENT/README.txt" <<'README'
OMEdit optional analysis component - Linux x86_64, glibc

Overlay share/omedit/analysis onto an OMEdit installation with Analysis Figures.
This component contains the Python plotting runtime, worker and validation tools;
it does not contain OMEdit or install into any host Python environment.

Provisioned with CPython 3.14.3 (uv python-build-standalone), Matplotlib 3.10.8,
NumPy 2.4.2 and Tornado 6.5.4. Exact transitive versions and allowed distribution
hashes are recorded in requirements-lock.txt. Package and Python license files
remain in the runtime; license-inventory.json locates them. Runtime and component
manifests record shipped file bytes. The uv provisioning executable is not shipped.

Validated in the pinned Debian 13.7 / glibc 2.41 linux/amd64 container recorded in
container-provenance.json, after relocation to a directory containing spaces.
The archive was extracted to another location and validated there as well.
Checks cover import origin/isolation, WebAgg token/cookie/Host checks and assets,
2D/3D/surface rendering, PNG/SVG/PDF exports, and protocol shutdown plus stdin EOF.
All 18 renderer tests and 8 staging-validator tests passed in this container.
Shared-library resolution for the relocated interpreter and native modules exercised
by exports is recorded in
the separate native-dependencies.json evidence artifact. No resolved dependency
references the original /opt/managed-python provisioning directory. System
libraries under /lib and /usr/lib remain OS prerequisites; they are not copied
into this runtime.

This is a headless runtime/worker check, not a Linux OMEdit GUI test or a claim
that this binary works on every Linux distribution, glibc version or architecture.
No macOS runtime is included or verified. Review redistribution licenses and run
the same checks on every supported OS image before release. Python and package
versions are pinned for comparison with the Windows artifact, not a promise that
these are the newest patch versions; update release inputs through normal review.

To verify after extraction (select a NEW output directory):
./share/omedit/analysis/runtime/bin/python3 -I -B ./validation/stage-analysis-runtime.py --validate-runtime ./share/omedit/analysis/runtime
./share/omedit/analysis/runtime/bin/python3 -I -B ./validation/test-analysis-deployment.py --runtime ./share/omedit/analysis/runtime --output ./deployment-evidence

No pip, Python, or packages are installed when OMEdit launches the worker.
README
cp /out/native-dependencies.json "$ANALYSIS_COMPONENT/"
"$python" -I -B - <<'PY'
import hashlib,json,os,pathlib
root=pathlib.Path(os.environ['ANALYSIS_COMPONENT'])
files={p.relative_to(root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(root.rglob('*')) if p.is_file() and '__pycache__' not in p.parts}
(root/'component-manifest.json').write_text(json.dumps({"version":1,"platform":"linux-x86_64-glibc","scope":"optional-analysis-component","linuxGuiTested":False,"cleanOsValidated":False,"files":files},indent=2,sort_keys=True)+'\n')
print(f'Component manifest contains {len(files)} files')
PY
tar --exclude='__pycache__' --exclude='*.pyc' -C /opt -czf /out/omedit-analysis-linux-x86_64-glibc.tar.gz 'OMEdit Analysis Linux Component'
mkdir -p '/opt/Archive Roundtrip Linux'
tar -xzf /out/omedit-analysis-linux-x86_64-glibc.tar.gz -C '/opt/Archive Roundtrip Linux'
roundtrip_component='/opt/Archive Roundtrip Linux/OMEdit Analysis Linux Component'
roundtrip_python="$roundtrip_component/share/omedit/analysis/runtime/bin/python3"
"$roundtrip_python" -I -B "$roundtrip_component/validation/stage-analysis-runtime.py" --validate-runtime "$roundtrip_component/share/omedit/analysis/runtime" | tee /out/staging-roundtrip.log
"$roundtrip_python" -I -B "$roundtrip_component/validation/test-analysis-deployment.py" --runtime "$roundtrip_component/share/omedit/analysis/runtime" --output /out/deployment-archive-roundtrip
ldd "$roundtrip_python" | tee /out/roundtrip-interpreter-ldd.log
if grep -E '/opt/managed-python|not found' /out/roundtrip-interpreter-ldd.log; then exit 1; fi
"$roundtrip_python" -I -B - "$roundtrip_component" <<'PY'
import hashlib,json,pathlib,re,sys
root=pathlib.Path(sys.argv[1])
expected=json.loads((root/'component-manifest.json').read_text())['files']
actual={p.relative_to(root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(root.rglob('*')) if p.is_file() and '__pycache__' not in p.parts and p!=root/'component-manifest.json'}
assert actual==expected,'Archive component manifest mismatch'
archive=pathlib.Path('/out/omedit-analysis-linux-x86_64-glibc.tar.gz')
hash=hashlib.sha256(archive.read_bytes()).hexdigest()
pathlib.Path('/out/archive-sha256.txt').write_text(f'{hash}  {archive.name}\n')
summary={"ok":True,"archive":archive.name,"bytes":archive.stat().st_size,"sha256":hash,"verifiedComponentFiles":len(expected),"relocatedDeploymentReport":"deployment-relocated/report.json","archiveRoundtripReport":"deployment-archive-roundtrip/report.json","linuxGuiTested":False,"cleanOsValidated":False,"unitTests":{name:int(re.search(r"Ran (\d+) tests?",pathlib.Path('/out',name+'-tests.log').read_text()).group(1)) for name in ('renderer','stage')}}
pathlib.Path('/out/summary.json').write_text(json.dumps(summary,indent=2,sort_keys=True)+'\n')
print(json.dumps(summary,indent=2,sort_keys=True))
PY
