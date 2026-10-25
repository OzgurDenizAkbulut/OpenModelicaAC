# OMEdit local autocomplete implementation plan

The [FRD](OMEdit-local-autocomplete-FRD.md) defines the feature. Work on branch `feature/omedit-local-autocomplete`. Use GPT-6.1 Sol agents with medium reasoning, at most three parallel implementation lanes. The coordinator owns contracts, build registration, integration review, and verification.

## Shared contract

`LocalCompletionSettings` is a value type in `OMEdit/OMEditLIB/Autocomplete/LocalCompletionSettings.h`. `OptionsDialog::localCompletionSettings()` returns the saved/applied settings, and `localCompletionSettingsChanged()` announces changes. The client is independent of OMEdit singletons. The controller accepts the current `QPlainTextEdit` and its `QCompleter`, owns request scheduling and preview lifecycle, and is instantiated only by ModelicaEditor.

## Lane 1 Inline presentation and editor behavior

- Implement a true multiline inline preview without changing the source document.
- Reuse Qt text layout; validate displaced suffix/following lines and source isolation.
- Implement scheduling and request snapshot validation, popup precedence, configurable manual action, Tab acceptance, Escape dismissal and one-step undo.
- Connect ModelicaEditor to applied settings.
- Add focused Qt rendering/lifecycle tests.
- Own new presentation/controller files, ModelicaEditor hooks and any necessary narrow BaseEditor hooks. Do not edit OptionsDialog, the client, or shared CMake files.

## Lane 2 Local inference client

- Implement loopback validation and normalization, no-proxy/no-redirect asynchronous HTTP, request cancellation and deadline.
- Build the Qwen Base FIM prompt and parse bounded non-streamed completions.
- Expose a connection/model test without editor text.
- Add fake-server transport and prompt/result tests.
- Own client files and their focused tests. Do not edit settings UI, editor hooks or shared CMake files.

## Lane 3 Autocomplete options

- Add Tools > Options > Autocomplete with saved/applied settings and change notification.
- Preserve the existing built-in completion preference and key.
- Provide enable/automatic/server/model/manual-shortcut controls and connection test status.
- Match existing options save/cancel/reset behavior and localization conventions.
- Own OptionsDialog.h/.cpp and any necessary OptionsDefaults.h change. Do not edit editor, client, or shared CMake files.

## Integration sequence

1. Record requirements and publish interfaces before parallel edits.
2. Run the three independent lanes. Resolve interface changes through the coordinator.
3. Register new source/test files, inspect the combined diff and resolve build failures.
4. Run focused deterministic tests and a rendering review, then build/install OMEdit if its dependencies are available.
5. Perform live local model smoke tests only when a suitable server/model is present. Record unverified runtime/quality claims explicitly.
6. Update this plan with actual delivery status and remaining work. Do not claim complete FRD acceptance from a partial build or client-only tests.

## Status

The configuration expansion approved on 2026-10-24 adds server presets, named server/model profiles, advertised-model discovery, and Advanced controls for timing, output/context budgets, temperature/instructions, ghost appearance and automatic triggers/acceptance keys. Existing defaults migrate unchanged. Profiles contain only endpoint/model selection; global generation and editor preferences remain independent. No troubleshooting panel or server-management dependency is added.

- FRD and task breakdown: written.
- Parallel implementation: first version implemented in all three lanes.
- New autocomplete classes and standalone tests: compiled with GCC 16.1 and Qt 6.11.1; both test suites pass.
- Preview visual check: rendered PNG inspected; source-isolation and geometry assertions pass.
- Existing editor and Options translation units: syntax checks and Options moc compilation pass with WebEngine/animation disabled.
- Live model protocol: LM Studio smoke test returned responses; Modelica quality issues found. Ollama remains unverified.
- Windows development application: CMake install completed; runtime staged against the installed 1.27 compiler with documented legacy API limitations and animation disabled.
- Full-application offscreen smoke: Modelica load/check, Autocomplete defaults, Save/persistence, Cancel, validation and rendered Options page pass using isolated preferences.
- Native interactive acceptance, controlled CPU benchmarks and a matching-current-compiler build: pending.

Detailed evidence and limitations are in the [validation record](OMEdit-local-autocomplete-validation.md). The focused tests have a [standalone build guide](../OMEdit/Testsuite/LocalAutocomplete/README.md).
