# Optional OMEdit analysis runtime

Analysis figures run a separate Python worker. OMEdit never runs pip or downloads
packages. A distribution can omit the runtime; the figure builder then explains
how to obtain the optional component. The worker itself is installed under
`share/omedit/analysis`.

## Provision and stage on Windows

Use a trusted, pre-provisioned 64-bit CPython distribution with Matplotlib, NumPy,
Tornado and their dependencies installed. Match the requirements in
`OMEdit/Resources/analysis/requirements.txt`. Keep the upstream Python and package
licenses with the distribution. Provision dependencies at packaging time; do not
install into an end user's Python or depend on an end user's site packages.

The staging tool only reads the source installation and writes a new directory:

```powershell
python tools/development/stage-analysis-runtime.py --python C:/provisioned/python.exe --destination C:/staging/analysis-runtime
python tools/development/stage-analysis-runtime.py --validate-runtime C:/staging/analysis-runtime
```

The source interpreter's existing package search path is used to locate installed
distributions. Staging copies the standard library, interpreter DLLs, extension
modules, Microsoft runtime DLLs present beside Python, and the required Python
package dependency closure, including wheel companion DLL directories and package
license metadata. It does not copy arbitrary unrelated site packages or invoke
an installer. `python._pth` limits the staged interpreter to its own directories;
user site packages, Python environment variables and registry paths are ignored.

Validation uses the copied interpreter in isolated mode, imports NumPy, Tornado
and WebAgg, and renders a PNG. This exercises relevant native extension and DLL
loading. Package versions and SHA-256 hashes for every shipped file are recorded
in `analysis-runtime-manifest.json`; later validation rejects changed files.
Keep this manifest with release artifacts. Reproducing an artifact requires the
same trusted Python/package inputs; the manifest records those exact versions
and bytes, rather than relying on the broad development requirement ranges.

The requested runtime directory is the trust boundary. Validation rejects
external interpreter/file/directory symlinks, Python base prefixes, imported
modules and search paths outside it. An in-place check reports
`relocationTested: false`: only copying and executing at another location can
exercise relocation. Neither check alone proves clean-OS deployment.

Do not redistribute a virtual environment as a relocatable runtime: its base
interpreter paths commonly point outside the directory. Automatic staging uses
the base CPython distribution and produces a self-contained directory instead.
Review redistribution licenses and validate on a clean supported Windows image
before releasing. Import/render checks on a developer workstation do not prove
that every operating-system prerequisite exists on every supported machine.

## Install

Configure the ordinary OMEdit build with:

```text
-DOM_OMEDIT_ANALYSIS_RUNTIME_DIR=C:/staging/analysis-runtime
```

CMake validates this directory, then the install target copies it to
`share/omedit/analysis/runtime`. The expected interpreter is `python.exe` on
Windows or `bin/python3` on Unix. The worker is installed alongside the runtime.
The runtime option is empty by default. A downstream package may distribute the
runtime directory separately from OMEdit; place it at the same relative path.

Unix packagers must provide a relocatable pre-provisioned runtime using their
platform packaging tools. The validation command accepts it, but automatic
Windows staging does not construct a portable Unix Python distribution. Check
native shared-library dependencies, executable permissions, package licensing
and relocation on each supported platform. The disposable Linux recipe below
exercises one Debian/glibc environment. No macOS runtime has been staged or
verified by this workflow.

## Transfer a Windows component for clean-machine testing

The component packager reads a validated Windows runtime and creates a fresh
directory containing an unpacked component, ZIP and archive SHA256:

```powershell
python tools/development/package-analysis-runtime.py --runtime C:/staging/analysis-runtime --output C:/staging/analysis-component-NEW
```

The archive contains `share/omedit/analysis/runtime`, the worker, upstream
license metadata, file hashes, a README and portable validation scripts. It is
an optional component to overlay onto an OMEdit installation with this feature;
it is not a complete OMEdit installer. Nothing is uploaded or published.

Extract the entire archive on the target Windows machine and follow its README.
Its own interpreter runs the checks, so host Python is unnecessary. Preserve
the generated report, stderr log and exported figures as evidence.

## Portable deployment probe

On Windows, Linux or macOS, use the pre-provisioned runtime for that OS:

```text
python tools/development/test-analysis-deployment.py --runtime /path/to/runtime --worker /path/to/analysis_worker.py --output /path/to/new-evidence
```

The driver needs only Python's standard library and can be run with the bundled
interpreter. It launches a separate isolated interpreter with a minimal PATH,
fresh home/cache and no developer Python or native-library environment overrides.
It checks module origins, local authenticated WebAgg assets, rejection of missing
or invalid credentials, mixed 2D/3D rendering, PNG/SVG/PDF dimensions/signatures,
and shutdown immediately followed by stdin EOF. Operations have timeouts.

The same script is portable; successful execution on Windows does not validate
an untested Linux/macOS runtime. Native package builders still must supply those
runtime artifacts and verify OS dependencies, relocation and signing where used.

## Disposable Linux provisioning and packaging

`tools/development/analysis-linux-container.sh` runs inside the pinned Astral uv
image. It provisions managed CPython 3.14.3 and exact top-level plotting package
versions matching the Windows artifact, records a hash-locked transitive package
set, and copies the resulting runtime to a path containing spaces. It validates
the worker, records native-library resolution, packages a tar archive and repeats
validation after extracting that archive to another location.

Run from the repository root with Docker available and a new evidence directory:

```powershell
$sourceRoot = (Get-Location).Path
$linuxEvidence = (New-Item -ItemType Directory -Path 'build_cmake/development-128/analysis-linux-container-NEW').FullName
$uvImage = 'ghcr.io/astral-sh/uv:0.13.5-trixie-slim@sha256:5a9953f569006028f707a8a436609cf5589e140eca41c0ba564dbd7d962c3b46'
docker run --rm --entrypoint /bin/bash `
  --mount "type=bind,source=$sourceRoot/OMEdit/Resources/analysis,target=/analysis-source,readonly" `
  --mount "type=bind,source=$sourceRoot/tools/development,target=/validation-source,readonly" `
  --mount "type=bind,source=$linuxEvidence,target=/out" `
  $uvImage /validation-source/analysis-linux-container.sh
```

Network access is needed for provisioning. The Python installation override
applies only to the explicitly selected managed interpreter inside the disposable
container. Host Python is untouched. Source mounts are read-only; only the new
evidence directory is writable on the host. The container is removed afterward;
Docker may retain its downloaded image cache.

The output tarball is a Linux x86_64/glibc optional component, not an OMEdit
application package. A passing result on the recorded Debian image does not
establish compatibility with all Linux distributions or validate the Linux GUI.
The package includes its own README, runtime licenses, hash manifests and
portable checks for downstream target-machine validation.

For development only, `OMEDIT_ANALYSIS_PYTHON` selects an interpreter explicitly.
The compiled source-worker fallback supports a build tree without changing the
installed worker lookup. Normal users need neither variable nor Python setup.

## Focused checks

Analysis data and renderer tests are registered even with
`OM_OMEDIT_DEVELOPMENT_TESTS_ONLY=ON`. `AnalysisFigureSmoke` is an explicit build
target. The renderer test uses the configured managed runtime when one is given;
otherwise it uses the CMake-discovered Python interpreter and requires the same
imports there. Run tests after the build, separately from compilation.

The workspace validation artifact on 2026-10-25 used an isolated copy of CPython
3.14.3, Matplotlib 3.10.8, NumPy 2.4.2 and Tornado 6.5.4, with 2,859 files. It
passed imports and PNG rendering. The staging tool made no changes to the source
Python or user installations. The generated runtime is a local deployment
artifact, not a file to commit to the repository.
