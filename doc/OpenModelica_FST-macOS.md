# OpenModelica_FST for Apple Silicon

The `OpenModelica FST macOS Apple Silicon` workflow builds the current source
revision on GitHub's `macos-15` ARM64 runner. Pushes to `build/fst-macos-arm64`
trigger it; manual dispatch is also configured. No release is published.

Successful runs upload `OpenModelica_FST-macos-arm64`, containing the DMG,
SHA-256 checksum and payload manifest. A separate evidence artifact retains
build logs, dependency versions, source/submodule revisions and acceptance logs,
including on failure. Artifacts expire after 14 days.

The workflow caches the native installation only after its autocomplete tests
pass. Packaging retries verify the cached source inputs and Homebrew dependency
versions before reuse. The distribution manifest records the native compiler
source revision separately from the packaging revision.

The app requires Apple Silicon and macOS 15 or newer. Simulation additionally
requires Apple Xcode Command Line Tools (`xcode-select --install`). The app
includes the compiler, editor and isolated analysis Python runtime. Local LLM
autocomplete requires a separately configured compatible server and model.
Fortran, dynamic optimization, 3D animation and OMSimulator are disabled in this
initial build, consistent with the reduced development configuration where
applicable. It is ad-hoc signed and is not Apple notarized.

The native build entry point is:

```sh
bash tools/development/build-macos-distribution.sh
```

It installs build dependencies through Homebrew and creates a fresh build under
`build_cmake/fst-macos`. Use a fresh checkout for each full run. The workflow
uses the repository's pinned submodule revisions, records Homebrew versions and
freezes the resolved analysis requirements. Homebrew and Python dependency
resolution are not yet locked across runs.

Packaging uses Qt's macdeployqt, relocates remaining native dependencies, checks
ARM64 slices and dependency containment, and verifies the ad-hoc signature.
Before creating the DMG, it moves the app into a path containing spaces, hides
the original install prefix, runs a Modelica simulation, validates isolated
Python imports and rendering, and checks the GUI help entry point. The build
also runs the focused autocomplete tests. This does not replace interactive
testing on a clean Mac, and GUI help is not a full GUI/WebEngine acceptance test.

## Verified development artifact (2026-10-26)

[Run 40339828496](https://github.com/OzgurDenizAkbulut/OpenModelicaAC/actions/runs/40339828496)
passed and uploaded `OpenModelica_FST-1.28-dev-a76ee409ca-macos-arm64.dmg`.
Size: 373,525,435 bytes. SHA-256 (also verified after local download):
`2ed8cd6585672a1fe1985dd09189018c80dcc36e52c1bc6b45dec3208a384c79`.
The native compiler reports `7fe8c0a54c-cmake`; packaging revision is
`a76ee409ca`. The compiler source is unchanged by the subsequent packaging fixes.

Verification covers ARM64 binaries, bundled dynamic dependencies, ad-hoc
signature verification, a relocated simulation with the original installation
hidden, isolated Python rendering, the GUI help entry point, and disk-image
verification. The simulation log records successful initialization and execution.
The native installation's two autocomplete tests passed in
[run 40328813297](https://github.com/OzgurDenizAkbulut/OpenModelicaAC/actions/runs/40328813297),
and all three packaging regressions passed on the final macOS run.

The bundled analysis runtime contains Python 3.12.14, Matplotlib 3.11.2,
NumPy 2.5.4 and Tornado 6.6. Interactive GUI/WebEngine testing on a clean Mac
and Apple notarization remain unperformed. The validation runner physically
moves the app before checking Python; the shared runtime helper's
`relocationTested: false` field describes that helper alone, not the enclosing
packaging test.

This artifact includes the FST working-tree snapshot captured at the start of
the distribution task. Additional analysis-figure changes appeared in the
original checkout during the build; those concurrent changes are not included.

References: [GitHub runner architecture](https://docs.github.com/en/actions/reference/runners/github-hosted-runners),
[Qt deployment](https://doc.qt.io/qt-6/macos-deployment.html).
