# OpenModelica_FST Windows installer

The development installer packages the matching 1.28-dev compiler and customized
OMEdit with its simulation tools. It installs for the current Windows account,
under `%LOCALAPPDATA%/Programs/OpenModelica_FST-1.28-Development` by default. Administrator
rights, a development SDK and a separate Python installation are not required.

## Setup choices

| Component | Default | Behavior |
| --- | --- | --- |
| OMEdit and simulation tools | Required | Editor, compiler and C/C++/Fortran tools, including the build tools used for FMU export. |
| Analysis figures | Selected | Installs the managed Python plotting runtime. Without it, OMEdit explains that the optional component is needed. |
| Local LLM autocomplete | Selected | Enables the integration. A compatible local server and model must be configured separately under Options > Autocomplete. |
| Desktop shortcut | Unselected | Adds a shortcut for the current account. A Start menu shortcut is always created. |

Deselecting local LLM autocomplete disables its options page and prevents its
network requests, including when saved preferences previously enabled it.
Built-in text completion is independent of this component. The application reads
`share/omedit/components.ini` at startup. Selecting the component permits the
usual application preferences, including a saved disabled preference.

Setup does not download a model or server, install into an existing Python,
change the persistent PATH, or register file associations. Modelica library
packages use the normal OpenModelica package manager and shared user cache;
the installer does not include an offline Modelica library cache.

Use the Start menu shortcut or `OpenModelica_FST.exe` in the installation root. The native
launcher prepares a process-local environment using the bundled tools and Qt
assets. It does not depend on Git, PowerShell or the source workspace.

## Updates and removal

The first version supports installation into a fresh empty directory. Setup
rejects an existing OpenModelica_FST registration and occupied destinations. To update
or change components, uninstall and reinstall.

Uninstall removes only the shipped files, its own component configuration,
shortcuts and uninstall registration. Models, user-created files, the shared
library cache and preferences are retained. Keep models outside the program
directory. Program-directory links/junctions are rejected during removal.

Development preferences remain at
`%APPDATA%/openmodelica-ac/omedit-development.ini`, separate from official OMEdit.
This legacy settings path is retained so existing OMEdit AC preferences carry
over to OpenModelica_FST without copying or resetting them. Uninstall the former
OMEdit AC development build before installing the renamed build. The workspace
remains at `C:/OpenModelica_AC/OpenModelicaAC`; it does not determine the product name.

## Build the artifact

Build/install the application and affected focused tests using the
[development workflow](OMEdit-1.28-development.md), then stop compilation before
running acceptance checks. The packaging command reads that installation; it
does not rebuild the compiler or mutate the toolchain.

From the repository root:

```powershell
python tools/development/package-windows-installer.py --makensis C:/OpenModelica_AC/build-tools/nsis-3.13/nsis-3.13/makensis.exe --toolchain-overlay C:/OpenModelica_AC/build-tools/installer-inputs/overlay --output build_cmake/development-128/installer-fst-001
```

Use a fresh output directory. `--stage-only` prepares the payload without
compiling it; `--compile-staged` compiles an existing stage after rechecking its
file hashes. Zlib is the default compression for fast iteration;
`--compression lzma` produces a smaller artifact with a much slower build.
`--installer-name <filename.exe>` keeps a corrected build separate from an earlier
installer. Python 3.12+ is required on the packaging machine. NSIS itself is
only required on the packaging machine.

The stage contains application files, the optional analysis runtime, generated
file-by-file removal instructions and `distribution-manifest.json`. The manifest
records source revision, the GUI source diff hash, installed binary hashes,
toolchain package versions/licenses/dependencies, source-recipe links and every
payload file hash. Package-owned files are selected from the existing pacman
database, resolving virtual dependency providers. Manuals and Python caches are
excluded; license trees and package records are retained.
The MSYS base runtime and MSYS GNU Make are explicit inputs, in addition to
MinGW Make. The stage generates a portable `etc/fstab` drive mapping, and setup
creates the MSYS temporary directory. These are needed by the shell and CMake's
MSYS Makefiles generator during simulation and FMU builds.

The current toolchain input lacks `ucrt64/lib/libicu.a`. The overlay supplies it
from the exact matching MSYS2 CRT package, with SHA-256 verified against the
installed package's mtree record:

`8e28ed04301cb6128bac63e809904857ba6f9ce89401adb0923a863c84cb1943`.

The package was fetched from
`https://repo.msys2.org/mingw/ucrt64/mingw-w64-ucrt-x86_64-crt-14.0.0.r37.g2bfe61fba-1-any.pkg.tar.zst`.
Its archive SHA-256 is
`f2a4e6c9c8108f3ecb2ed9541812e63f15b2b4cf2839c8f1aa77871657c91d8b`.
No source toolchain file was replaced.

## Acceptance

```powershell
python tools/development/test-windows-installer.py --installer <Setup.exe> --build build_cmake/development-128 --output build_cmake/development-128/installer-fst-acceptance-001 --modelica-library-path C:/Users/ZGR/AppData/Roaming/.openmodelica/libraries
```

The runner installs the actual EXE into directories containing spaces, checks
payload hashes and bundled compiler-library resolution, and runs compiler,
simulation, result-reader, FMU, autocomplete, native GUI and analysis-runtime
acceptance with developer tool directories removed from PATH. It also checks
component deselection, existing-install/destination rejection, uninstall,
user-model preservation, and unchanged original preferences, associations and
user environment. It requires no existing OpenModelica_FST installer registration.
Compiler revision checks default to the packaged source commit. When checking
an older artifact that retained a compiler configured at an earlier revision,
pass `--expected-compiler-revision <prefix>` explicitly.

Silent setup accepts `/S`, `/NOAUTOCOMPLETE`, `/NOANALYSIS`, and a final
unquoted `/D=<destination>`. Silent uninstall accepts `/S`.

The directory-check regression can run independently of the full payload:

```powershell
python tools/development/test-installer-directory.py --makensis C:/OpenModelica_AC/build-tools/nsis-3.13/nsis-3.13/makensis.exe --output build_cmake/development-128/installer-directory-check
```

It accepts new and existing empty folders, including the directory created by
setup itself, and rejects folders containing files or subdirectories. The first
installer incorrectly normalized a nonexistent path to an empty string and
used a wildcard existence check that also matched empty directories. The
corrected guard preserves the destination and enumerates actual entries.

The artifact is an unsigned local development build. Host acceptance is separate
from clean Windows VM validation; the current machine has the original
development files on disk. macOS packaging, signing, automatic updates, the
algorithmic debugger and other optional upstream applications are not included.

## Verified OpenModelica_FST artifact (2026-10-26)

Use [OpenModelica_FST-1.28.0-dev-6d31d8c9cb-win64-Setup.exe](../build_cmake/development-128/installer-fst-001/OpenModelica_FST-1.28.0-dev-6d31d8c9cb-win64-Setup.exe)
(597,587,458 bytes). SHA-256:
`e1cb19e11bdc004e9ef21ccc07f7f60a9f531d4fe93a13f7e270cc3e57c20a3d`.
This replaces the earlier OMEdit AC artifacts for local installation.

The application title, About menu, Windows executable properties, launcher,
installer, shortcuts and uninstall registration use OpenModelica_FST. The
existing preferences path is retained as described above.

[`installer-fst-acceptance-002/report.json`](../build_cmake/development-128/installer-fst-acceptance-002/report.json)
records passing full/minimal installation, all 16,609 payload hashes,
simulation/MSL/FMU export, autocomplete, offscreen/native GUI, analysis-runtime,
component deselection, uninstall and occupied-folder checks. Existing preferences,
file associations and user environment were unchanged. The Start menu shortcut
and installed Windows product properties were also verified in
[`branding-verification.json`](../build_cmake/development-128/installer-fst-acceptance-002/branding-verification.json).
All four directory cases passed in `installer-fst-directory-001`.

The rebuilt compiler reports `v1.28.0-dev.1493+g6d31d8c9cb.cmake`.
The first acceptance attempt stopped at the previous compiler-revision default;
the installer runner now checks the packaged source commit, with an explicit
override for older compiler builds. The same installer passed the rerun.

Normal interactive startup with existing preferences remains for the manual
reinstall; automated native GUI checks used isolated settings. Clean Windows VM
validation remains unperformed. The test installations were removed successfully.

## Previous local artifact (2026-10-25)

This artifact predates the OpenModelica_FST rename and is superseded.

The previous build was `build_cmake/development-128/installer-001/OMEdit-AC-1.28.0-dev-6d31d8c9cb-win64-r3-Setup.exe`
(597,587,183 bytes). SHA-256:
`a4be414dd660bd86352bad8b6b341e0723f5a0f482a9b823eb1ff5c44477f491`.
The r1 and r2 artifacts are superseded.

[`installer-acceptance-003/report.json`](../build_cmake/development-128/installer-acceptance-003/report.json)
records a passing actual-installer run: full/minimal installation, both optional
components deselected, simulation/MSL/FMU export, 38 client and 17 inline tests,
offscreen/native application checks, analysis exports, uninstall, occupied-folder
rejection and preservation of existing preferences, associations and user PATH.
The directory guard separately passed all four cases in `installer-directory-003`.
All 16,609 payload file hashes matched after full installation.

Normal launcher `--help` passed. Normal interactive startup was skipped to
preserve existing development preferences; the native full-app harness used
isolated settings. Clean Windows VM validation remains unperformed.
