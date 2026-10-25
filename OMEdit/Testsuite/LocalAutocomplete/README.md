# Local autocomplete tests

These tests exercise the local completion client and inline presentation/controller without starting the OpenModelica compiler or an LLM server. The regular OMEdit testsuite also registers them.

## Standalone build

Provide a C++17 compiler and Qt 6 Core, Gui, Widgets, Network and Test development packages:

```sh
cmake -S OMEdit/Testsuite/LocalAutocomplete -B build_cmake/local-autocomplete -G Ninja -DCMAKE_PREFIX_PATH=/path/to/qt
cmake --build build_cmake/local-autocomplete --parallel 3
ctest --test-dir build_cmake/local-autocomplete --output-on-failure
```

On Windows, place the matching Qt SDK `bin` and compiler/runtime dependency directories on the process PATH. If required, set `QT_PLUGIN_PATH` to the SDK's plugin directory. CTest selects the offscreen platform. Test executables also accept the usual Qt Test logger arguments, for example `-o client-results.txt,txt`.

`LocalCompletionClientTest` uses an in-process HTTP server to check request payloads, loopback restrictions, proxy/redirect rejection, deadlines, cancellation, response limits and model availability checks. `InlineCompletionTest` checks source isolation, displaced multiline text, wrapping/folding geometry, debounce, explicit shortcuts, acceptance/undo, stale results and dismissal behavior.

Configuration coverage includes model discovery without a selected model, stale discovery cancellation, larger token/context/line budgets, custom instructions, temperature, and a configured deadline beyond five seconds. Editor tests cover opacity/italics, alternate acceptance chords, deletion-trigger selection, minimum identifier length, explicit-request bypass and settings changes canceling pending work.

Set `OMEDIT_INLINE_PREVIEW_SCREENSHOT` to an absolute PNG path to save the multiline preview from `InlineCompletionTest::multilineIsolationAndGeometry`. The test renders a plain Qt editor; it does not prove full OMEdit gutter, syntax highlighting or Options integration.

## Optional live runtime check

Run a local server separately and select an already downloaded Qwen2.5-Coder Base model. The script uses only synthetic Modelica prompts and never starts a server or downloads a model. A completion request may cause the server to load the selected model.

```sh
python OMEdit/Testsuite/LocalAutocomplete/live_smoke.py --endpoint http://127.0.0.1:1234/v1 --model qwen2.5-coder-1.5b
```

The script records response text and elapsed time. Successful HTTP responses are not proof of valid Modelica or useful completion quality. Its Python socket timeout is a smoke-test bound; the production Qt client additionally enforces a total request deadline.

## Remaining application checks

Build and run the installed OMEdit application to verify the Options page, Save/Cancel/restart persistence, gutter alignment, fold indicators, source scroll restoration, other editor types and native-platform shortcuts. Repeat the live protocol check against Ollama independently. CPU latency evaluation must explicitly control the inference runtime's hardware configuration.
