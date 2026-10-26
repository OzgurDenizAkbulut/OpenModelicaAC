# OpenModelica_FST for Apple Silicon

The `OpenModelica FST macOS Apple Silicon` workflow builds the current source
revision on GitHub's `macos-15` ARM64 runner. Pushes to `build/fst-macos-arm64`
trigger it; manual dispatch is also configured. No release is published.

Successful runs upload `OpenModelica_FST-macos-arm64`, containing the DMG,
SHA-256 checksum and payload manifest. A separate evidence artifact retains
build logs, dependency versions, source/submodule revisions and acceptance logs,
including on failure. Artifacts expire after 14 days.

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

Status: workflow prepared; a successful macOS run is required before this can be
described as a validated distribution.

References: [GitHub runner architecture](https://docs.github.com/en/actions/reference/runners/github-hosted-runners),
[Qt deployment](https://doc.qt.io/qt-6/macos-deployment.html).
