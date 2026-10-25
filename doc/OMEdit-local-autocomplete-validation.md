# OMEdit local autocomplete validation

## Implemented first version

The feature lives on `feature/omedit-local-autocomplete`. Three GPT-6.1 Sol agents at medium reasoning implemented the inline presentation/controller, local HTTP client, and options page. The coordinator registered build/test targets and reviewed their integration. No VS Code code was copied; the implementation uses Qt's existing layout with an independent presentation document.

The inline view displaces suffix and following code without changing the source document. It can scroll its own presentation to make the entire suggestion visible. If a suggestion still cannot fit, it is suppressed. Scrolling, font/size changes and other unsupported geometry interactions dismiss the preview. The first click while a preview is displayed dismisses it without performing an action at a potentially displaced coordinate.

## Verified locally

- Configured and compiled all new autocomplete classes and both focused test executables with GCC 16.1.0 and Qt 6.11.1.
- Both CTest suites passed: LocalCompletionClientTest and InlineCompletionTest. Detailed Qt logs reported 34 and 11 passing entries respectively, including each suite's setup and cleanup.
- Inspected a generated multiline preview PNG. Geometry and source-isolation assertions also pass.
- GCC syntax checks pass for the complete changed OptionsDialog.cpp, ModelicaEditor.cpp and BaseEditor.cpp translation units, plus generated OptionsDialog moc output. These checks use installed OpenModelica headers with the supported WebEngine/animation-disabled configuration; they do not link the application.
- `git diff --check` and Python smoke-script compilation pass.

The isolated SDK is at `C:/OpenModelica_AC/qt-autocomplete-sdk`. Its Qt base archive was downloaded from the official MSYS2 mirror and checked against the package's published SHA256: `526e8f55938ebb50e4e1206d65e13ac850a7b5045cdf4b14d2b2fc124cecf406`. The existing OpenModelica installation was not modified. Build/test artifacts are under the ignored `build_cmake/local-autocomplete` directory; integration syntax-check scripts and logs are under `build_cmake/integration-check`.

## Configuration expansion validation — 2026-10-24

Implemented the five approved configuration groups without adding a troubleshooting panel or new runtime dependency. Both focused CTest suites passed after compiling the expanded client, controller and preview (17.45 seconds total). The transport tests cover larger context/token/line budgets, temperature, instruction formatting, endpoint-only model discovery, cancellation and requests lasting beyond the previous five-second cap. Editor tests cover the configurable acceptance keys, opacity/italics and automatic-trigger restrictions while preserving explicit requests and single-undo acceptance.

The installed Windows application was rebuilt successfully. The expanded full-application offscreen smoke passes for all 18 completion settings: legacy-default migration, nondefault Save/readback, Cancel and applied-setting isolation. It also exercises the real Qt HTTP model-discovery path against a loopback fixture and verifies profile creation, switching, update, removal, selected-profile persistence and cancellation. URL changes clear the previous server's discovered model choices while retaining editable text. Shortcut collisions and oversized instructions are rejected; disabled configurations do not silently truncate instructions.

Basic, expanded Advanced and scrolled Advanced layouts were rendered and inspected. Screenshots are `build_cmake/omedit-smoke/autocomplete-options-basic.png`, `autocomplete-options-advanced.png` and `autocomplete-options-advanced-bottom.png`. No user's preferences were changed by these tests. LM Studio was unavailable on port 1234 during this run, so this expansion's discovery verification used the deterministic local fixture; the earlier live result below remains historical evidence only.

## Earlier live LM Studio smoke test

The existing server at `http://127.0.0.1:1234/v1` advertised `qwen2.5-coder-1.5b`, a downloaded code2lora Q4_K_M GGUF of approximately 986 MB. No model was loaded before the first request. Three synthetic FIM requests returned successfully:

| Case | Observed elapsed time | Result assessment |
| --- | --- | --- |
| Declaration | 4,986 ms | Generated an inappropriate block with non-Modelica syntax and did not respect the intended suffix |
| Equation | 118 ms | Returned `-0.5*x`, a plausible continuation of the provided derivative equation |
| Multiline equations | 243 ms | Returned apostrophe derivative notation (`x'`, `y'`), which is not valid Modelica derivative syntax |

These observations establish a working runtime endpoint for the request shape, not model quality. The smoke test used the server's existing hardware configuration; the times are not CPU-only benchmark results. Keep model quality evaluation separate from transport acceptance. A small general code model may need a better model choice or Modelica-specific training to be useful.

## Native development build

The local Windows build uses the repository's OMEdit, OMPlot/qwt and OMParser CMake targets through an ignored harness at `build_cmake/omedit-local-src`. It links the installed OpenModelica 1.27 C compiler and installs into `build_cmake/install_cmake`. The reproducible entry point is `build_cmake/build-local-omedit.ps1`; runtime staging and launching are in `build_cmake/stage-omedit-runtime.ps1` and `build_cmake/run-local-omedit.ps1`.

The approved isolated Rust toolchain is under `C:/OpenModelica_AC/build-tools`. Both the result-file library and compiler archive library built successfully with Rust 1.99.1, targeting `x86_64-pc-windows-gnu`. Qt base is 6.11.1; WebEngine uses the installed 6.11.0 modules. Animation is disabled in this development build. The original OpenModelica installation remains unchanged.

The installed compiler predates several APIs in this checkout. Harness-only adapters preserve its older behavior; they are not part of the autocomplete product change. Compiler progress/cancellation, class-diagram generation and component-condition editing are unavailable in this development build. Model-instance references fall back to the existing JSON API. FMU export accepts only the legacy default integration method. Full acceptance of those features requires a matching compiler build.

The CMake `install` target completed successfully. The installed executable is `build_cmake/install_cmake/bin/OMEdit.exe`. Runtime dependency staging succeeded, and junctions under the isolated installation connect its compiler data, headers and toolchain to the existing distribution. That distribution must remain installed; this is not a standalone redistributable package.

The legacy build also skips the optional `-d=omedit` host hint, which its 1.27 compiler does not recognize. Regression checks verify that startup and clearing command-line options produce no unknown-debug-flag error. The corrected application was rebuilt and installed; an already-running session must restart to use it.

The real Qt client connection test passed against LM Studio at `http://127.0.0.1:1234/v1` with model identifier `qwen2.5-coder-1.5b`. The saved Ollama defaults (`11434` and `qwen2.5-coder:1.5b-base`) do not identify that LM Studio server/model; both fields must be changed in Options when using it.

`build_cmake/run-smoke.py` passed with exit code 0 for the installed OMEdit help command, installed compiler version command, and `OMEditLocalSmoke`. The latter constructs the actual OMEdit application and compiler in testsuite mode and verifies:

- Loading and checking a synthetic Modelica model.
- Autocomplete defaults, controls, applied-setting isolation, Save notification and persisted values.
- Cancel restoring saved controls without applying pending edits.
- Preservation of the existing built-in completion preference.
- Rejection of remote endpoints and unmodified typing shortcuts.
- Saving a disabled configuration with blank server/model fields.

The smoke test uses temporary isolated settings and Qt's offscreen platform. Its rendered Options page, `build_cmake/omedit-smoke/autocomplete-options.png`, was inspected. Evidence logs are in the same directory. Both focused CTest suites passed again after the full build. The offscreen run emitted platform size-hint and WebEngine profile teardown warnings; interactive WebEngine behavior was not validated.

## Outstanding acceptance work

- Native interactive keyboard behavior, gutter/fold indicators, large documents and WebEngine rendering. The desktop-control helper was unavailable after retry and reset; the successful offscreen application test does not establish native UI acceptance.
- Live Ollama verification; no Ollama server was running during this work.
- Controlled CPU benchmarks and a representative Modelica completion corpus.
- Cross-platform GUI checks on Linux and macOS.

The focused tests deliberately do not start OMEdit or its compiler. They cannot substitute for full application acceptance. See the [FRD](OMEdit-local-autocomplete-FRD.md) and [implementation plan](OMEdit-local-autocomplete-implementation.md).
