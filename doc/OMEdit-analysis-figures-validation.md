# Analysis figures validation

## User-reported loading and interaction regressions (2026-10-25)

Investigation used the existing development CSV fixture, the installed result
reader, failing regression tests and the real OMEdit GUI harness. No Qt defect
was established. Existing local changes were retained.

- CSV: `analysis-fixture.csv` and `analysis-demo.csv` both expose all eight
  columns through the installed reader. The real Open Result path also loads
  them. The Analysis Figure **Add result…** path selected the source without
  registering variables in the browser. The strengthened GUI regression fails
  at that point in `analysis-csv-reproduction/gui-offscreen.log`. It now validates
  the result and registers its variables before selecting the source.
- Plot sizing: after a WebAgg viewport resize to 460 by 640, the next render
  restored export dimensions (480 by 320 in the minimal fixture). This caused
  a resize exchange with the browser on each frame. Live viewport dimensions
  and device pixel ratio now remain separate from export settings.
- 3D controls: constrained layout recomputed subplot bounds while rotating,
  moving the axes under the pointer. Refresh preserved elevation/azimuth/roll
  and X/Y limits but discarded manually selected Z limits. Layout is now
  settled after content/viewport changes and fixed during interaction; manual
  limits are retained on all three axes. Cosmetic legend/axis labels no longer
  count as a changed variable assignment when deciding whether to retain the
  camera and limits. Real WebAgg drag and zoom events are tested across a
  playback update.
- Playback: time-only updates rebuilt axes and reset canvas geometry. The
  WebAgg client also allowed pending PNG decodes to share mutable image/mode
  state. Regression coverage checks viewport stability, persistent axes during
  trajectory playback, and ordered image/mode processing.

Surface playback replaces its plotted data while retaining the axes and
colorbar. Automatic bounds are recomputed from the replacement surface so
scrubbing backward can shrink them; manually selected bounds remain fixed.

The C++ worker client correlates replies by request ID, the figure window
serializes its renders, and the Python command dispatcher runs on Tornado's
event loop. The demonstrated asynchronous ordering fault was in browser image
decoding, where the previous WebAgg handler replaced a still-pending image.

Evidence paths in this section are relative to
`build_cmake/development-128/`.

Final verification:

- Build/install and GUI harness build passed with two jobs:
  `logs/20261025-133921-build-1.log` and `logs/20261025-133921-build-2.log`.
  The final Python-only updates were installed using OMEdit's generated local
  CMake install script. Source and installed worker SHA256 both equal
  `e44676e8ba89761f168a874cff22089af3684518e2f450bad653f7b4ad78ab9e`.
- `analysis-bugs-acceptance/data.log`: 13 passed, no failures or skips.
- `analysis-bugs-acceptance/renderer.log`: 27 passed, no failures or skips.
  `analysis-bugs-regression/renderer-with-node.log` independently records all
  27 passing tests, including the pending-image decoder regression.
- `analysis-bugs-gui-verified/report.json`: offscreen and native Windows GUI
  runs passed. Both check Open Result, Add result, malformed CSV rejection,
  variable assignment, 2D/3D/surface exports and every decoded frame during
  successive playback updates. Screenshots for all three plot types in both
  platform modes were visually reviewed.
- The CSV GUI helper uses the Qt file dialog and enters the path in its
  filename field. An initial helper using `selectFile()` stalled when the
  asynchronously populated second dialog still had an empty selection; a
  debugger stack confirmed it was waiting in the dialog, before registration.
  The final harness build is `logs/20261025-134853-build.log`.
- The installed OMEdit executable matches its successful install manifest:
  SHA256 `70f08610400cd8a4ca27f7be2a2cb1f5fed91e0776308592e4c5a27ca0266f7a`.
  Qt lint and inline ownership/threading/API review found no introduced C++
  correctness issues. The existing WebEngine teardown warnings remain.

This follow-up validates Windows on the development workstation. It does not
extend the earlier Linux/headless results to these changes or claim macOS GUI
validation. No Computer Use skill was used.

Status: Windows build/install, automated acceptance and final screenshot review
passed on 2026-10-25. Windows relocation and Debian Linux headless runtime checks
also passed; clean Windows, macOS and Linux GUI checks remain separate.

## Scope

The implementation was developed in four parallel GPT-6.1 Sol medium lanes.
The coordinator integrated the lanes and owns compilation and acceptance.
Existing autocomplete/development-profile changes were preserved.

This change affects OMEdit and its optional renderer, not Modelica compiler or
simulation equations. Validation targets result reading, GUI integration and
renderer behavior; no compiler-runtime changes require a three-runtime sweep.

## Reproduce on the Windows development candidate

Configure `OM_OMEDIT_ANALYSIS_RUNTIME_DIR` to a validated isolated runtime as
described in [runtime packaging](OMEdit-analysis-runtime-packaging.md).

```
python tools/development/build-development.py --stage gui --jobs 2
python tools/development/build-development.py --stage build --target AnalysisFigureSmoke --jobs 2
python tools/development/test-analysis-figures.py --build build_cmake/development-128 --install build_cmake/development-128/install_cmake --output build_cmake/development-128/analysis-acceptance-NEW --native
```

Use a fresh evidence directory. Compilation and tests are serialized through
the development build lock. The GUI checks use isolated QSettings and launch
the real OMEdit application in testsuite mode. The installed managed runtime is
used without a developer interpreter override.

## Checks

- Data tests cover event timestamps, missing variables, rectangular arrays,
  nonzero-start playback, conversions, retention, package relocation and CSV.
- Renderer tests cover chart types, invalid panels, local authentication,
  WebSockets, multiple windows, failure recovery, exports and process shutdown.
- Display reduction tests verify extrema/gaps and full-data exports.
- GUI smoke opens the menu action, assigns actual result variables, renders
  time/3D/surface figures, exports PNGs and verifies a painted embedded canvas.
- Offscreen and native platform runs capture screenshots for visual inspection.

## Evidence from this workstation

Evidence directories below are relative to `build_cmake/development-128/`.

- Build/install: `logs/20261025-004659-build-1.log` and
  `logs/20261025-004659-build-2.log`; strengthened GUI smoke build:
  `logs/20261025-005306-build.log`.
- Feature data acceptance: `analysis-acceptance-005/data.log`: 13 passed
  (11 test methods plus setup/cleanup).
- Final renderer suite: `analysis-renderer-layout-final/tests.log`: 17 passed,
  including viewport decoration bounds at 460 by 640 pixels.
- Final GUI acceptance: `analysis-acceptance-006/report.json`. Both offscreen
  and native Windows GUI checks passed with the installed managed runtime and
  no developer Python override. Time, trajectory and surface screenshots in
  its `gui-native/` and `gui-offscreen/` directories were visually reviewed.
  The worker SHA256 at that acceptance point matched the source:
  `706c2c8ad5c4c0fd0dc7e1d1f1309d3c84c1431eddf09a3863c1391a3d8cda3b`.
- Existing development regression: `acceptance-analysis-regression-001/report.json`.
  All eight processes passed: compiler version, compiler simulation/result/FMU,
  Modelica 4.1.0 simulation, installed OMEdit help, completion client, inline
  completion, and the existing full-app GUI smoke on both platform modes.
  The latter includes ordinary plotting and diagram behavior.

The feature GUI checks verify the real menu action, Variables Browser drag
payload, 2D/3D/surface rendering and PNG export, stored-time playback, assignment
retention across plot types, and Cancel on an unsaved window. Browser canvas
pixels and geometry are checked before screenshots are captured.

Visual review caught and corrected export mutation of the live canvas and
Matplotlib inline resize styles overriding viewport constraints. The export
regression verifies that PNG, SVG and PDF leave the live renderer dimensions
and pixel buffer unchanged. Constrained layout and horizontal 3D color bars
keep decorations readable in narrow windows. Final screenshots were reviewed
separately from the automated assertions.

Offscreen GUI tests use `--disable-gpu`; native Windows tests use the normal
graphics path. Qt emits existing WebEngine profile teardown warnings in both
the development and analysis harnesses; these do not fail the checks.

## Performance

Final measurements in `analysis-renderer-layout-final/benchmark.json` used
Windows 11, CPython 3.14.3 and Matplotlib 3.10.8 on this workstation:

| Case | Initial render | Redraw | Full-data PNG export |
| --- | ---: | ---: | ---: |
| 10 curves, 100,000 samples each | 0.557 s | 0.136 s | 3.498 s |
| 100 by 100 surface | 0.204 s | 0.188 s | 1.402 s |

These measure the renderer, excluding C++ data preparation, transfer and
browser display latency. Display reduction is enabled; exports retain all
samples. No end-to-end frame-rate claim is made. Earlier measurements before
the layout correction are superseded by these results.

## Platform boundaries

The staged runtime is a relocatable Windows CPython copy with required package
files, native dependencies, licenses and a hash manifest. Unix runtime packaging
and a clean Windows machine without development tools require separate checks.
Matplotlib's documented 3D depth-ordering limits remain applicable.

## Deployment follow-up (2026-10-25)

The following checks extend the evidence above. Paths remain relative to
`build_cmake/development-128/`.

- `analysis-deployment-hardening-final/validator-tests.log`: 8 passing tests
  covering manifest checks, symlink escapes and Python base/runtime origin.
- `analysis-deployment-hardening-final/renderer-tests.log`: 18 passing tests.
  A deployment probe exposed a shutdown/EOF race that stopped Tornado's loop
  during connection cleanup. The stop callback is now idempotent, with a
  regression covering immediate stdin EOF and an active WebSocket.
- `analysis-relocated-gui-001/report.json`: both offscreen and native GUI checks
  passed after copying the full installation to `Analysis Relocation 001/OMEdit`.
  PATH contained only that installation's `bin`, Windows and System32; OMDEV
  and developer SDK/toolchain paths were removed. This exercised spaces in paths.
  The GUI harness executable remains in the build tree; it uses the relocated
  installed dependencies and worker. This is not an installed-launcher test.
- `analysis-deployment-relocated-001/report.json`: the relocated managed runtime
  passed origin/isolation, authenticated local HTTP/assets, mixed 2D/3D rendering,
  PNG/SVG/PDF exports and shutdown with immediate EOF. Worker stderr was empty.
- `analysis-component-001/omedit-analysis-windows.zip`: optional Windows component,
  47,830,233 bytes, including runtime, worker, licenses, manifests and portable
  validation scripts. Archive SHA256:
  `3bf1307ca3a1e815cefea111b0fc5f77de7db46a79164f888eb7b9fce46e053c`.
- `analysis-deployment-zip-001/report.json`: the ZIP was extracted to another
  directory containing spaces; all 2,864 component file hashes matched. Its own
  bundled Python ran the bundled deployment validator successfully. The mixed
  2D/3D export was visually inspected.

Current source, installed and relocated worker SHA256:
`928aacb484bd90e8953ad5f42753fe0be3b2342be44fc78088afce371f58d48e`.
This supersedes the earlier worker hash; the layout and benchmark behavior did
not change in the lifecycle fix.

These are relocation and environment-isolation checks on the developer host,
not a clean Windows VM. The stripped GUI run reports missing MinGW toolchain
paths: plotting works, but this copy is not evidence of a complete portable
compiler/simulation distribution. The component ZIP is not an OMEdit installer.

Available environment inventory: Windows Sandbox was unavailable, Hyper-V VM
enumeration was denied, and no macOS runner was exposed. Docker's existing
Linux images were inspected in disposable, network-disabled containers; none
provided Python. No packages were installed and no Linux/macOS runtime was
claimed validated at that inventory point. The user subsequently authorized
preparing a disposable Linux container and requested local checks plus portable
test bundles. Clean Windows and macOS checks still require suitable hosts.

## Linux container and portable handoff bundles

Final Linux evidence is in `analysis-linux-container-003/`. The disposable image
was pinned to
`ghcr.io/astral-sh/uv:0.13.5-trixie-slim@sha256:5a9953f569006028f707a8a436609cf5589e140eca41c0ba564dbd7d962c3b46`:
Debian 13.7, glibc 2.41, x86_64 on the WSL2 Linux kernel. Managed CPython 3.14.3,
Matplotlib 3.10.8, NumPy 2.4.2 and Tornado 6.5.4 match the Windows top-level
versions. `requirements-lock.txt` records pinned transitive dependencies and
permitted distribution hashes; `container-provenance.json` records actual inputs.

- `renderer-tests.log`: 18 passed; `stage-tests.log`: 8 passed.
- `deployment-relocated/report.json` and
  `deployment-archive-roundtrip/report.json`: passed. The latter used the
  extracted component's own interpreter and bundled validation script.
- The interpreter and 12 exercised native modules passed shared-library
  resolution checks, with no missing dependencies or references to the original
  `/opt/managed-python` directory. See `native-dependencies.json`.
- All 7,712 component file hashes matched after tar extraction. Runtime license
  inventory retained 83 license/notice documents. The exported mixed 2D/3D
  figure was visually inspected.
- `omedit-analysis-linux-x86_64-glibc.tar.gz`: 77,611,509 bytes, SHA256
  `1050613875c078cf7fd1f36dbe6eee2809683c6f0b0c0654a43c479f3cd1031a`.

The reproducible workflow is in `tools/development/analysis-linux-container.sh`
and the [packaging guide](OMEdit-analysis-runtime-packaging.md). Dependency
installation occurred only inside the approved disposable container; host Python
was not changed. Containers were removed after use. This proves the headless
runtime on the recorded environment, not Linux GUI behavior or compatibility
with every Linux distribution, glibc version or CPU architecture.

The platform-neutral source kit is
`analysis-validation-kit-002/omedit-analysis-validation-kit.zip` (31,723 bytes),
SHA256 `bdc9ba2c34796e1f85c5c836c120a3a34d609e7d05c50b819130891e6678c830`.
It includes the worker, tests, validators, Linux recipe and instructions for
packagers supplying their own trusted Windows/Linux/macOS runtime. Its file
hashes and ZIP integrity were verified; it contains no runtime or OMEdit binary.

All deliverables are local artifacts; nothing was published. Clean Windows and
macOS host validation can use the supplied bundles when such machines are
available. A complete application installer and compiler toolchain distribution
remain outside these optional-component artifacts.
