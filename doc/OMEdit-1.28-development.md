# OMEdit 1.28 development workflow

This is the operational handoff for the [agreed development plan](OMEdit-1.28-development-plan.md).
The matching source-built compiler and development GUI are installed. Core,
focused autocomplete, offscreen/native full-app and incremental-build checks
passed, including the normal installed-application startup probe through the
actual launcher. Live model-backend acceptance is separate.
The [verification record](#verification-record) distinguishes completed evidence
from the workflow commands below.

Start with the [developer checklist](#developer-checklist) for setup and daily
work. The [lessons learned](#lessons-learned) explain the failures encountered
during the migration and the practices that address them.

## Source and directories

The upstream source base is `40d9aae9b8c95e64b9ecad3cdc044189cf2df171`
(`v1.28.0-dev-1489-g40d9aae9b8`). Local feature commits on
`feature/omedit-local-autocomplete` checkpoint autocomplete, the development
environment, analysis figures and the splash artwork. Use `git log` and
`git status` to identify the current checkpoint and any later local edits.
Preserve the working tree. No upstream update or push is part of this setup.

The launcher checks the installed manifest against the checkout's current
commit by default. Reinstall after advancing the source commit; the build
wrapper records that commit in the successful-install manifest. Use
`-ExpectedRevision` explicitly when intentionally launching an older verified
installation. The compiler's version string can still identify the configured
upstream source base; it is separate from the manifest's local feature commit.

Run commands below in PowerShell from `C:\OpenModelica_AC\OpenModelicaAC`.
The build wrapper uses these fixed candidate locations:

| Purpose | Path |
| --- | --- |
| Build | `build_cmake/development-128` |
| Installation | `build_cmake/development-128/install_cmake` |
| Logs and build lock | `build_cmake/development-128/logs` |
| Successful-install manifest | `build_cmake/development-128/installed-development.json` |

The candidate is independent of the older `build_cmake/install_cmake` deployment.
Do not install through that older path or its junctions. Run installed executables
from the candidate, so compiler libraries, builtin definitions and data resolve
relative to the matching installation.

## Developer checklist

### Set up a new development candidate

1. Record the source revision, branch, local changes and submodule pins. Preserve
   the working deployment and preferences before changing build configuration.
   The selected baseline is a **1.28 development revision**, not a stable release.
2. Provision the [workspace dependencies](#workspace-dependencies). Use a short
   toolchain path without spaces and retain the `OMDEV/tools/msys/ucrt64` layout.
   Confirm Python 3.12+, the GNU C/C++/Fortran tools, Java, Rust support tools and
   the complete Qt SDK. The wrapper selects these tools explicitly.
3. Stage the genuine WebEngine and ICU assets using the documented helpers.
   Keep their source/hash manifests. The helpers supplement an existing SDK;
   they do not download or provision the entire toolchain on a fresh machine.
4. [Build and install the compiler](#build-the-compiler-then-the-gui), initialize
   required submodules at their pins, then run compiler-only acceptance against
   that installation. Resolve compiler failures before adding the GUI.
5. Build/install the GUI with the development profile and compile the three
   focused test targets. Run [installed-candidate validation](#validate-the-installed-candidate)
   with native GUI coverage and the explicit MSL package directory. Add
   `--normal-startup` for a fresh development preference identity.
6. Inspect the acceptance report and GUI captures, verify original files remain
   unchanged, and verify an OMEdit-only rebuild preserves compiler artifacts.
   Adopt the candidate through its launcher only after these checks pass.

### Daily OMEdit development

Run from `C:\OpenModelica_AC\OpenModelicaAC` in PowerShell. This assumes the
candidate has already been configured with the GUI and focused tests enabled.

1. Check `git status --short` and `git rev-parse HEAD`; preserve unrelated local
   work. Keep the configured candidate on its selected baseline.
2. Close the candidate OMEdit before installing over its Windows executable/DLLs.
   The build scripts do not terminate a user's modeling session for them.
3. Edit the source and build/install incrementally:

   ```powershell
   python tools/development/build-development.py --stage build --target OMEdit --jobs 2
   python tools/development/build-development.py --stage build --target install --jobs 2
   ```

4. After autocomplete/editor, shared GUI-library or test changes, rebuild the
   affected test executables before running them. To refresh all three, the
   wrapper runs these targets sequentially:

   ```powershell
   python tools/development/build-development.py --stage build --target LocalCompletionClientTest InlineCompletionTest OMEditDevelopmentSmoke --jobs 2
   ```

5. Stop compilation, run the validation command below with a fresh evidence
   directory, and inspect its report. For routine validation after preferences
   exist, omit `--normal-startup`; the offscreen/native tests isolate their
   settings explicitly. Do not delete development preferences to satisfy the
   first-launch probe.
6. Launch with `./tools/development/launch-development.ps1`. Configure a compatible
   local completion endpoint/model in the Autocomplete options and start that
   backend when live suggestions are needed. A saved disabled preference takes
   precedence over the development profile's enabled default.

Use `--stage build` for ordinary incremental work. `--stage core` reconfigures
with GUI clients disabled; it is not the daily OMEdit build command. Use the GUI
configuration stage when deliberately changing dependencies or profile options.
Retain `install` in the workflow: it deploys the changed app and refreshes the
manifest required by the launcher. Launching an old installed executable can
otherwise make a successful source build look ineffective.

### Checks to choose for a change

| Change | Required development check |
| --- | --- |
| Completion transport, preview or editor integration | Rebuild relevant focused targets; run client/inline tests and real-editor smoke, including ghost, Tab and undo. |
| GUI layout, diagrams or plotting | Rebuild the GUI and full-app test; run native GUI coverage and inspect the relevant screenshots. |
| Compiler, generated scripting API or runtime | Build/install the matching source targets; rerun compiler/MSL/simulation/FMU and GUI integration acceptance. Follow the repository's relevant compiler regression-test requirements too. |
| Qt deployment, toolchain or launcher | Check installed assets, loaded DLL provenance and normal startup with a fresh preference identity; test-executable startup alone is insufficient. |
| Documentation only | Check commands against scripts, links and whitespace; no application rebuild is needed. |

## Lessons learned

These are observations from the 2026-10-24 migration. The
[verification record](#verification-record) retains the successful evidence.

| Observation | Practice for future development |
| --- | --- |
| The checkout was already 1.28-dev, but the previous GUI harness used the installed 1.27 compiler and compatibility stubs. | Treat the source revision, generated Qt API, compiler DLLs, runtime and installation data as one baseline. Verify loaded paths and behavior, not just the GUI version label. |
| Some old deployment directories were junctions into the official installation. | Use the independent candidate prefix and audit links before installation. Preserve the official installation and preferences. |
| A dependency's Fortran/LTO build failed through the toolchain path under `Program Files`. | Use the workspace toolchain path without spaces. Keep the exact `OMDEV/tools/msys` structure needed for subsequent model compilation. |
| Fork-relative submodule URLs failed to resolve. | Override the affected URLs in local Git configuration to the verified upstream repositories while retaining the tracked pins; do not advance dependencies to make initialization succeed. |
| Disabling the experimental Rust compiler did not remove the need for Rust support libraries. | Keep the normal C/MetaModelica compiler selected and provision its required Rust support toolchain separately. |
| Qt headers/import libraries were present, but `uic` could not start because ICU 78 DLLs were missing. | Diagnose the actual loader dependency. Stage matching genuine libraries with hashes and confirm tools run with the intended PATH. File presence alone does not establish SDK completeness. |
| `windeployqt` succeeded while omitting the WebEngine helper/resources/locales. | Explicitly deploy those assets and verify their installed locations. Keep `qt.conf` consistent with plugins, translations and helper executables. |
| The source-linked GUI test resolved locales differently from installed OMEdit. | Give the test its own generated Qt configuration pointing to the candidate. Also validate the real launcher with default installed-plugin discovery. |
| Qt diagnostic output was initially absent from captured logs. | Force stderr logging in the runner and retain explicit QtTest result files. Require success markers and positive DLL observations, not merely empty logs or a process exit. |
| Screenshot capture disturbed the preview test before Tab acceptance. | Settle the real editor layout before requesting completion, capture the preview, and assert it remains visible. Keep production invalidation behavior intact and visually inspect the evidence. |
| A startup probe assigned different `TEMP` and `TMP` directories and looked in the wrong one. | Use one private directory for both variables. Audit Windows preference locations separately; environment overrides alone are not proof of QSettings isolation. |
| Changing Java during the first GUI configuration regenerated the parser and relinked compiler artifacts. | Stabilize tool selections before measuring incremental performance. Use hashes and timestamps to distinguish compiler changes from GUI-only work. |
| Make's overall percentage reached 98–100% while substantial GUI compilation remained. | Track actual target/object progress and available memory. Keep one build owner, at most two jobs in this setup, bounded subprocesses, and no concurrent tests. |
| A successful install and passing tests prove different things. | Publish the install manifest only after success; retain a separate acceptance report. A failed later build must not replace the launcher's successful-install provenance. |
| Synthetic completion tests passed with no live model server running. | Track integration correctness and live backend availability/quality separately. Enabling the feature does not start a server or select a usable model. |

The measured 13.74-second incremental build touched only `OMEditGUI/main.cpp`;
it is evidence of compiler reuse, not a time estimate for every editor change.
Keep upstream baseline updates deliberate and separate from routine OMEdit
development. The current wrapper has a fixed candidate directory, so preparing
another baseline also requires configuring a new build/install destination.
Installer and macOS work need their own deployment and acceptance checks.

## Workspace dependencies

Build and runtime execution use workspace-local dependency copies. Provisioning
sources, including the read-only source of the ICU copies below, are recorded
separately from the paths used by the running build and application:

| Dependency | Current source |
| --- | --- |
| UCRT64 GNU C/C++/Fortran, CMake, Make and native dependencies | `C:/OpenModelica_AC/build-tools/omdev128/tools/msys`, copied into an independent toolchain tree |
| Java runtime | `C:/OpenModelica_AC/build-tools/jre21` |
| Qt SDK | `C:/OpenModelica_AC/qt-autocomplete-sdk/ucrt64`: Qt base and related modules 6.11.1; WebEngine 6.11.0 |
| ICU 78 runtime | Three genuine generic ICU DLLs copied into the Qt SDK's `bin`; per-file source paths and hashes in `icu-runtime-provenance.json` |
| Rust support toolchain | `C:/OpenModelica_AC/build-tools/rustup` and `C:/OpenModelica_AC/build-tools/cargo`, `stable-x86_64-pc-windows-gnu` |
| Python | Python 3.12 or later on `PATH` |

The Qt SDK retains its MSYS2 package archives in `qt-autocomplete-sdk`. Its missing
WebEngine CMake package metadata is supplied by the tracked
[SDK adapters](../tools/development/cmake/README.md). These import real Qt DLLs;
they do not adapt compiler APIs. The WebEngine helper, six resource files and
53 locale packs were staged from the independent toolchain, with per-file SHA-256
provenance in `qt-autocomplete-sdk/ucrt64/webengine-runtime-provenance.json`.
This Qt patch-version combination has now been exercised by the passing offscreen
and native full-application checks described in the verification record.

If the SDK needs to be provisioned again, this command stages only those missing
WebEngine assets, verifies hashes and refuses to replace differing existing files:

```powershell
python tools/development/stage-webengine-sdk.py --sdk C:/OpenModelica_AC/qt-autocomplete-sdk/ucrt64 --toolchain-prefix C:/OpenModelica_AC/build-tools/omdev128/tools/msys/ucrt64
```

The first GUI configure exposed missing ICU dependencies: Qt's `uic` exited with
Windows loader error `0xc0000135`. The dedicated SDK and independent toolchain
lacked `libicudt78.dll`, `libicuin78.dll` and `libicuuc78.dll`, which this Qt build
requires. Genuine copies were provisioned from the original installation's
read-only `bin` using:

```powershell
python tools/development/stage-icu-sdk.py --source-bin 'C:/Program Files/OpenModelica1.27.0-64bit/bin'
```

The default destination is `C:/OpenModelica_AC/qt-autocomplete-sdk/ucrt64/bin`;
`--qt-root` selects another SDK. The script verifies hashes, refuses to replace
differing existing copies and writes
`C:/OpenModelica_AC/qt-autocomplete-sdk/ucrt64/icu-runtime-provenance.json`.
These are generic ICU libraries, not compiler binaries or compatibility stubs.
Their original source directory is not required on build/runtime `PATH` after
copying, and the original installation was not modified. With the copied DLLs,
`uic -h` passed using a restricted workspace-only toolchain/SDK `PATH`; evidence is
[`uic-workspace-dependencies.txt`](../build_cmake/development-128/evidence/uic-workspace-dependencies.txt).
That loader check does not establish GUI build or application acceptance.

The MINGW install rules explicitly deploy the selected SDK's genuine
`QtWebEngineProcess.exe`, `resources` and `qtwebengine_locales`: this SDK's imported
WebEngine targets were not fully classified by `windeployqt`. They also write
`bin/qt.conf` with the deployed binary/plugin/library-executable paths rooted in
`bin`, QML imports in `qml` and translations in `share/qt6/translations`.
`qoffscreen.dll` is explicitly required and installed only when OMEdit tests are
enabled. The full-app test executable has its own generated `qt.conf` pointing
to candidate `bin`, so running the test from the build tree uses the installed
Qt/WebEngine layout. These rules use real SDK assets and leave compiler API
generation to the matching source build.

The build wrapper records revision, selected tools, tool versions, cache selections
and commands in timestamped logs/provenance files. It explicitly selects the GNU
compilers and independent Make executable. Its `--msys`, `--qt`, `--java`,
`--cargo-home` and `--rustup-home` arguments override the workspace defaults.

## Build the compiler, then the GUI

Only one owner runs compilation. Build one target at a time, and stop compilation
before running tests. The scripts share a candidate build lock; an active build
or validation causes another invocation to refuse work. Start with one job;
use two only when memory permits. `--timeout` defaults to 21,600 seconds per
subprocess, and `--jobs` accepts only `1` or `2`.

Confirm the source revision, then configure, build and install the compiler:

```powershell
git rev-parse HEAD
python tools/development/build-development.py --stage core --initialize --jobs 1
```

`--initialize` initializes the required submodules at the repository pins, including
the bootstrap compiler and selected nested third-party dependencies. It does not
update the baseline or submodule pins. Omit it on subsequent builds when those
dependencies are already initialized. The PowerShell wrapper accepts the same
arguments, for example `./tools/development/build-development.ps1 --stage core --jobs 1`.

After the installed compiler is ready, an optional compiler-only acceptance run
can validate it before GUI work. Use a fresh output directory each time:

```powershell
python tools/development/test-development.py --build C:/OpenModelica_AC/OpenModelicaAC/build_cmake/development-128 --install C:/OpenModelica_AC/OpenModelicaAC/build_cmake/development-128/install_cmake --toolchain-bin C:/OpenModelica_AC/build-tools/omdev128/tools/msys/ucrt64/bin --toolchain-bin C:/OpenModelica_AC/build-tools/omdev128/tools/msys/usr/bin --omdev C:/OpenModelica_AC/build-tools/omdev128 --output C:/OpenModelica_AC/OpenModelicaAC/build_cmake/development-128/acceptance-core-next --compiler-only
```

Configure, build and install the GUI into the same candidate:

```powershell
python tools/development/build-development.py --stage gui --jobs 2
```

This uses the normal top-level build and matching generated Qt scripting API.
The GUI stage enables `OM_OMEDIT_DEVELOPMENT_PROFILE`, focused development tests,
and the SDK WebEngine adapters. `OM_QT_DEPLOYMENT_PREFIX` selects the dedicated
SDK for both OMEdit and OMPlot deployment: its deployment tool, Qt DLLs, plugins
and QML modules take precedence over MSYS fallback dependencies.

The experimental Rust compiler is disabled (`OM_OMC_ENABLE_RUST=OFF`); required
Rust support libraries are still built. Current exclusions also include animation,
the C++ simulation runtime, ParModelica, OMSimulator, OMShell, OMNotebook, OMSens Qt
and the compiler regression testsuite. These exclusions define this development
candidate, not a complete distribution or full regression qualification.

Build the three focused test executables explicitly, one target per invocation:

```powershell
python tools/development/build-development.py --stage build --target LocalCompletionClientTest --jobs 2
python tools/development/build-development.py --stage build --target InlineCompletionTest --jobs 2
python tools/development/build-development.py --stage build --target OMEditDevelopmentSmoke --jobs 2
```

These commands compile the tests; they do not run them. `--stage build` keeps the
existing configuration. The focused targets are excluded from the GUI's default
build, so installation alone does not supply all three test executables.

## Validate the installed candidate

Run after compilation has stopped. The full command below checks the existing
Modelica 4.1.0 package cache read-only and includes a native Windows GUI smoke in
addition to the offscreen tests:

```powershell
python tools/development/test-development.py --build C:/OpenModelica_AC/OpenModelicaAC/build_cmake/development-128 --install C:/OpenModelica_AC/OpenModelicaAC/build_cmake/development-128/install_cmake --toolchain-bin C:/OpenModelica_AC/qt-autocomplete-sdk/ucrt64/bin --toolchain-bin C:/OpenModelica_AC/build-tools/omdev128/tools/msys/ucrt64/bin --toolchain-bin C:/OpenModelica_AC/build-tools/omdev128/tools/msys/usr/bin --omdev C:/OpenModelica_AC/build-tools/omdev128 --modelica-library-path C:/Users/ZGR/AppData/Roaming/.openmodelica/libraries --native-gui --output C:/OpenModelica_AC/OpenModelicaAC/build_cmake/development-128/acceptance-full-next
```

Choose a new output name each time; `acceptance-full-next` is an example, not a
directory to overwrite. Add `--normal-startup` for first-launch validation of a
fresh candidate preference identity. Routine runs retain the offscreen/native
checks without this optional probe.

That package directory currently contains `Modelica 4.1.0+maint.om` and its
dependencies. Supply the package directory, not the `Modelica .../package.mo`
file. The runner requests version `4.1.0` explicitly and checks its source path.
The MSL check is optional in the script, so omitting `--modelica-library-path`
does not establish MSL acceptance. Likewise, omit `--native-gui` only when
recording that native platform behavior remains unverified.
`--normal-startup` additionally probes the actual installed OMEdit through the
normal launcher. It is separate from the full-app test executable's testsuite mode.
The normal Windows probe audits native known-folder preference locations;
environment-variable changes alone do not isolate QSettings. It refuses existing
development application settings and can create first-launch development defaults,
while snapshotting official preferences to verify that they remain unchanged.
Keep the SDK `bin` fallback before MSYS toolchain directories, after candidate
`bin`; this preserves the intended Qt patch version when a dependency is needed.

The testsuite-mode checks use isolated preferences, home and temporary directories, a restricted
candidate/toolchain `PATH`, explicit compiler/data paths, junction auditing and
positive DLL provenance checks. Its coverage includes:

- Compiler revision, model loading/checking, model-instance and component-condition
  APIs, simulation, numerical result reading and a compiled FMI 2.0 ME export.
- Modelica 4.1.0 loading/checking/simulation when the explicit package path is given.
- Focused transport and inline completion tests.
- The real application, Options defaults/persistence, settings isolation, real
  diagram rendering of annotated components and a connection, Modelica editor
  completion request/ghost/Tab acceptance/undo, and Variables Browser plotting of
  the candidate simulation MAT file with numeric sample assertions.
- Native Windows platform coverage when requested.

The evidence directory contains `report.json`, process logs, model/result/FMU
artifacts and GUI PNGs, including `model-diagram.png`, `autocomplete-editor-ghost.png` and
`simulation-result-plot.png` under the GUI evidence directories. A passing standalone
CTest smoke without `OMEDIT_SMOKE_RESULT` skips plotting; the full runner supplies
the result and requires its plotting and editor assertions in the log.

The synthetic completion endpoint checks integration independently of any local
model server. At handoff preparation, no service was available on ports 1234 or
11434. Enabled autocomplete settings do not establish live backend availability or
model quality. Configure and start a compatible local server separately before
recording live completion acceptance.

## Launch and preferences

After successful GUI installation and acceptance, start the installed candidate:

```powershell
./tools/development/launch-development.ps1
```

An optional model can be opened with `-Model C:/path/to/Model.mo`. The launcher
accepts `-Install`, `-Manifest` (alias `-Provenance`), `-Omdev`, `-ToolchainBin`
and `-ModelicaLibraryPath` when explicit paths are needed. `-PassThru` returns the
launched process object; `-WindowStyle Normal` (the default) or `Hidden` controls
window presentation. Its normal defaults
use the candidate install and the toolchain recorded by the install manifest.
It constructs a restricted runtime environment and restores the invoking shell's
environment after launching OMEdit.

The application displays **OMEdit AC (Development)**. Preferences use
`openmodelica-ac/omedit-development.ini`; temporary files use the
`omedit-development` child of the normal OMEdit temporary directory. Official
OMEdit preferences are not imported. Local autocomplete defaults to enabled,
and an explicitly saved disabled value takes precedence. Normal builds retain
the stock settings identity and disabled local-autocomplete default.

The normal launcher intentionally reuses the standard user's Modelica package
cache. It does not replace `HOME` or `APPDATA`, and without `-ModelicaLibraryPath`
it clears inherited `OPENMODELICALIBRARY` so the compiler uses its normal package
lookup. Shared Modelica packages are distinct from isolated OMEdit preferences
and temporary files. The validation runner uses separate home/preferences and
only reuses the package directory explicitly supplied for the MSL check.

The launcher requires `installed-development.json` from a successful install.
That manifest records source HEAD, candidate prefix, GUI/profile selection,
toolchain paths, selected cache values and SHA-256 hashes of installed `omc.exe`
and `OMEdit.exe`. The launcher checks those executable hashes, expected revision,
profile, paths and reparse-point restrictions. A successful-install manifest is
not an acceptance report: review the separate validation evidence before adoption.

## Incremental OMEdit work

For an OMEdit-only source edit, retain the configured GUI candidate and run:

```powershell
python tools/development/build-development.py --stage build --target OMEdit --jobs 2
python tools/development/build-development.py --stage build --target install --jobs 2
```

The first command targets the GUI. The second installs the updated executable
and refreshes its successful-install manifest. Do not launch a build-tree OMEdit
or expect the launcher to accept a newly replaced executable against a stale
manifest. The completed incremental check confirmed unchanged compiler/runtime
hashes and modification timestamps; see its evidence below.
Repeat affected acceptance checks after changes that alter behavior or deployment.

## Deliberate baseline updates and remaining work

The build script records the current checkout revision; it does not fetch or
enforce the pinned HEAD. The launcher currently defaults `-ExpectedRevision` to
the full baseline hash, and validation defaults `--expected-revision` to
`40d9aae9b8` and `--expected-version` to `1.28.0-dev`.

For a future baseline update, preserve/review local autocomplete changes, select
the source revision and submodule pins deliberately, prepare a fresh candidate
build location, then rebuild and repeat acceptance before changing the adopted
launcher. The current wrapper's candidate directory is fixed in its source;
choosing a different fresh location requires updating that configuration.
`-ExpectedRevision <full-hash>` on the launcher and `--expected-revision <hash>`
on validation permit an explicitly reviewed alternative baseline; neither option
updates sources, rebuilds binaries or substitutes for validation. Update the
documented default pin only when the new baseline is accepted.

Windows installer creation is the next milestone. No installer, file-association
change, macOS distribution or macOS packaging has been delivered by this work.
The Windows source build and Qt deployment are not evidence of macOS readiness.

## Verification record

### Core acceptance: passed

On 2026-10-24, the first source build took 53 minutes 20 seconds, after a
182-second configure. A subsequent install completed in 75.48 seconds without
rebuilding the compiler. The corresponding logs are
[`20261024-132829-configure-core.log`](../build_cmake/development-128/logs/20261024-132829-configure-core.log),
[`20261024-132829-build.log`](../build_cmake/development-128/logs/20261024-132829-build.log)
and [`20261024-142505-build.log`](../build_cmake/development-128/logs/20261024-142505-build.log).

The installed compiler reports `v1.28.0-dev.1489+g40d9aae9b8.cmake`.
[`acceptance-core-001/report.json`](../build_cmake/development-128/acceptance-core-001/report.json)
records `status: passed` for the installed candidate. Its evidence establishes:

- Loading and checking the synthetic model, model-instance JSON retrieval and
  the conditional-component expression `useX`.
- Simulation and result reading: `x(1) = 0.36787945238337905` for `der(x) = -x`,
  with 12 result samples and successful numerical/sample assertions.
- FMI 2.0 model-exchange export with a model description and a nonempty platform
  binary inside the FMU.
- Modelica 4.1.0 loaded from the explicitly selected standard package directory;
  the composed Constant/FirstOrder model checked and simulated with its expected
  final value.
- The compiler installation/data path points into the candidate. Observed
  compiler/runtime/result DLLs came from candidate `bin`; no installation links
  escaped the candidate, and the report's link list is empty.

Detailed compiler and MSL output is retained in
[`compiler-smoke.log`](../build_cmake/development-128/acceptance-core-001/compiler-smoke.log)
and [`compiler-msl-smoke.log`](../build_cmake/development-128/acceptance-core-001/compiler-msl-smoke.log).
That evidence directory already exists; choose a new output name for another run.

### Full installed-candidate acceptance: passed

The matching development GUI was built and installed from the same checkout.
The GUI source build/install took 18 minutes 48.9 seconds; see
[`20261024-143256-build.log`](../build_cmake/development-128/logs/20261024-143256-build.log).
The installed manifest identifies the development profile and pinned source.

The primary evidence is
[`acceptance-full-004/report.json`](../build_cmake/development-128/acceptance-full-004/report.json):
`status: passed`, elapsed 47.7189785 seconds, and all nine processes returned zero.
This run includes the compiler/MSL checks, installed GUI help, both focused tests,
full-app offscreen/native GUI checks and actual installed-application startup.
Focused results were
[38 client tests passed](../build_cmake/development-128/acceptance-full-004/LocalCompletionClientTest-results.txt)
and [17 inline tests passed](../build_cmake/development-128/acceptance-full-004/InlineCompletionTest-results.txt),
with no failures or skips.

Full-app checks passed settings defaults/persistence and isolation, annotated
diagram components and connection rendering, a real editor's synthetic HTTP
completion/ghost/Tab acceptance/undo, and plotting of the candidate MAT result
with numeric sample assertions. Both
[`OMEditDevelopmentSmoke.log`](../build_cmake/development-128/acceptance-full-004/OMEditDevelopmentSmoke.log)
and [`OMEditDevelopmentSmoke-native.log`](../build_cmake/development-128/acceptance-full-004/OMEditDevelopmentSmoke-native.log)
contain `SMOKE PASSED`. The native
[diagram](../build_cmake/development-128/acceptance-full-004/gui-native/model-diagram.png),
[ghost](../build_cmake/development-128/acceptance-full-004/gui-native/autocomplete-editor-ghost.png)
and [plot](../build_cmake/development-128/acceptance-full-004/gui-native/simulation-result-plot.png)
captures are retained. Earlier native ghost and plot captures were also visually reviewed.

The [normal startup report](../build_cmake/development-128/acceptance-full-004/normal-startup/report.json)
and [startup log](../build_cmake/development-128/acceptance-full-004/installed-normal-startup.log)
record a passing launch of the actual installed OMEdit through the normal launcher.
An earlier run's temporary-log-path assertion was corrected in the probe before
this final run; passing testsuite-mode checks alone were not treated as normal
startup evidence.

Full-app test logs include nonfatal WebEngine profile teardown warnings after
`SMOKE PASSED`. They have not been investigated as a production defect. This
focused validation does not claim that the upstream regression suite passed.

### Incremental build and preservation: passed

An OMEdit-only rebuild target completed in 13.74 seconds, followed by a
30.67-second install. The experiment left `OMEditGUI/main.cpp` contents unchanged.
[`incremental-after.json`](../build_cmake/development-128/evidence/incremental-after.json)
records unchanged SHA-256 hashes **and modification timestamps** for `omc.exe`,
`libOpenModelicaCompiler.dll`, `libOpenModelicaRuntimeC.dll`,
`libOpenModelicaRuntimeMMC.dll` and `omc_result.dll`. Its linked build logs establish
that the compiler/runtime did not rebuild during this GUI-target experiment.

[`preservation-check.json`](../build_cmake/development-128/evidence/preservation-check.json)
records unchanged hashes for both executables in the older staged deployment,
both official 1.27 executables and the official user's `openmodelica/omedit.ini`.
This Windows-generated evidence JSON can be read as UTF-8 with BOM
(`utf-8-sig` in Python).

### Remaining limitations

Live model-backend availability and completion quality remain unverified because
no backend service was supplied. The deterministic transport and real-editor
checks passed against a synthetic local endpoint. Installer and macOS work remain
outside this milestone, as do the declared optional-feature exclusions above.
