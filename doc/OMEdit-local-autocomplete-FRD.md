# OMEdit local autocomplete functional requirements

Status: approved product direction; implementation and verification in progress.

## Purpose

Add optional, local LLM autocomplete to the OMEdit Modelica text editor. Present true inline, multiline ghost text using the existing Qt editor infrastructure. Minimize long-term maintenance and isolate the feature from the compiler, simulation runtime, and existing symbol completion.

## Agreed scope

- Multiline suggestions are required in the first version, including insertion before existing trailing text. The preview must make visual room for suggested text and move subsequent displayed code accordingly.
- Configure the feature under Tools > Options > Autocomplete.
- Inference runs in a separate, user-managed process on the same machine as OMEdit. OMEdit does not install models, start servers, or manage acceleration in this version.
- Support automatic and explicit requests. Default to 150 ms of typing inactivity and a 5-second request deadline. Both values are configurable; the deadline is not a latency promise.
- Existing completion retains Ctrl+Space and owns keyboard input while its popup is visible. Suppress ghost text while that popup is open.
- The configured acceptance key (Tab by default) accepts the entire visible suggestion as one undoable edit. Escape dismisses it. Without ghost text, ordinary editor behavior remains unchanged.
- The manual AI shortcut is separately configurable.
- Use a bounded prefix and suffix from the current Modelica editor buffer. Do not index projects or send other files.
- CPU inference is the initial validation baseline. Acceleration remains an external runtime concern.

## Implementation defaults

These are reversible engineering choices, not additional user constraints. Use one OpenAI-compatible text completion HTTP client for Ollama and LM Studio, initially with a Qwen2.5-Coder Base fill-in-the-middle profile and a configurable model identifier. Start disabled. Default to http://127.0.0.1:11434/v1 and qwen2.5-coder:1.5b-base; LM Studio users can set their server URL and exact loaded model identifier. Use Ctrl+Alt+Space as the initial configurable manual shortcut. Bound generated output to six logical lines and 128 tokens, prefix to 12,000 characters, and suffix to 4,000 characters. Never accept undisplayed output.

## Functional requirements

### FR 1 Settings

Provide enabled, automatic requests, server URL, model identifier, manual shortcut, and Test Connection controls. Clearly identify that the server must already be running locally. Preserve the existing built-in autocomplete preference and its saved key. Settings apply to open editors after saving and invalidate outstanding requests and previews. Invalid endpoint or empty model configuration must not generate requests.

#### Configuration expansion

The configuration expansion approved on 2026-10-24 adds explicit LM Studio, Ollama and Custom server presets; an editable model selector populated by Refresh Models; and named server profiles. A profile contains only a name, endpoint and model ID, leaving generation/editor preferences global. Changes to profiles and all controls are pending until Options is saved; Cancel restores saved state. Loading existing settings must not overwrite custom endpoints or invent model IDs. Discovery sends neither source nor instructions and does not establish FIM compatibility.

Technical settings live under a collapsible Advanced section:

| Control | Default | Range or choices |
| --- | --- | --- |
| Automatic delay | 150 ms | 50–2,000 ms |
| Request timeout | 5,000 ms | 250–60,000 ms |
| Suggestion lines | 6 | 1–50; entire suggestion must fit the preview |
| Output tokens | 128 | 1–2,048 |
| Source context before cursor | 12,000 characters | 0–64,000 |
| Source context after cursor | 4,000 characters | 0–16,000 |
| Temperature | 0 | 0–2 |
| Custom instructions | Empty | Up to 4,000 characters |
| Ghost-text opacity | 47% | 10–90% |
| Italic ghost text | Off | On/off |
| Accept suggestion | Tab | Tab, Ctrl+Right, Ctrl+Return |
| Request after Backspace/Delete | On | On/off |
| Minimum identifier characters | 0 | 0–16; automatic requests only |

A zero identifier minimum allows any eligible typed text change. A nonzero minimum requires that many contiguous letters, digits or underscores immediately before the caret. Explicit requests bypass these automatic trigger restrictions. The acceptance key must differ from the manual-request shortcut. Existing saved configurations use the defaults for newly introduced settings.

Custom instructions are prepended as Modelica line comments inside the Qwen FIM prefix, without modifying the source document. FIM marker delimiters inside instructions are neutralized. The selected temperature and size limits are sent to the server. Instructions guide the model but do not guarantee valid code. Saved server profiles do not change the FIM protocol. No troubleshooting dashboard is added.

### FR 2 Local transport

Accept only HTTP or HTTPS loopback endpoints on the OMEdit machine. Support IPv4 loopback, IPv6 loopback, and localhost mapped explicitly to loopback; reject remote hosts, credentials in URLs, query strings, and fragments. Disable proxies and redirects for these requests. A loopback URL cannot itself prove a third-party server performs local inference; document that users must select a locally hosted model, not a server's cloud-routing option.

Use asynchronous Qt networking with at most one active request per editor. Abort superseded requests and reject late results by request identity, editor revision, cursor anchor, focus, settings generation, and popup state. Do not block the GUI thread. Ordinary failures leave normal editing available; expose connection errors through the settings test rather than repeated modal dialogs while typing. The connection test sends no source code.

### FR 3 Completion generation

Send the selected model a raw fill-in-the-middle prompt containing the current bounded prefix and suffix. Request non-streamed text, the configured temperature (zero by default), and a bounded token count. Handle absent server, missing model, timeout, malformed response, empty output, excessive response size, and cancellation. Preserve generated indentation and newlines; normalize line endings. Reject chat prose/code fences and FIM control tokens rather than inserting protocol artifacts. Model-family protocols beyond Qwen are deferred until validated.

### FR 4 Preview isolation

Keep ghost text outside the source QTextDocument until acceptance. Preview changes must not dirty the document, change undo history, trigger LSP document updates, save generated text, or affect model validation. Render suggested text distinctly using the current font and theme. Show the actual insertion result, including displaced suffix and following lines. Maintain coherent wrapping, folding, gutter alignment, and scrolling. Dismiss before unsupported interactions rather than modifying the document or leaving stale geometry. A floating tooltip or text painted on top of unshifted source is not an acceptable replacement for inline rendering.

### FR 5 Interaction lifecycle

Debounce eligible text changes using the configured delay (150 ms by default) when enabled and automatic mode is on. Explicit requests bypass the debounce. Clear pending work and preview on Escape, incompatible edits, selection, cursor navigation, focus loss, editor closure, settings changes, and built-in completion popup activation. Do not immediately regenerate a dismissed suggestion without a new typing action or explicit request. Do not trigger on programmatic document loads or preview/acceptance bookkeeping. Read-only editors cannot request or accept suggestions.

Acceptance revalidates the original source revision and cursor location, inserts only the displayed suggestion at the anchor, preserves existing suffix text, places the caret after the inserted text, and groups the operation into a single undo step.

## Architecture and reuse

Use existing OMEdit QPlainTextEdit, QCompleter, QSettings, and Qt Network infrastructure. Keep networking, request scheduling, and ghost presentation in small separate classes. Do not route this through LSP or replace the editor with Monaco. Connect the feature only to ModelicaEditor. A presentation-only document may be used to let Qt lay out the proposed insertion while leaving the actual editor document unchanged, provided it satisfies FR 4 and coordinate/lifecycle tests.

VS Code's inline completion source is a design reference for separating ghost state, cancellation, acceptance, and view rendering. Its Monaco-specific view zones and injected text cannot be dropped into a Qt editor. Any copied or translated substantial source must retain the upstream MIT copyright and permission notice and record the exact source revision. Original Qt code inspired by the architecture must not be described as a direct port.

## Acceptance and validation

| Area | Required evidence |
| --- | --- |
| Preview | Multiline insertion before suffix; following code displaced; source text, modified state and undo untouched |
| Geometry | Wrapped/unwrapped lines, folded blocks, gutter, zoom/font changes and scrolling; unsupported actions dismiss safely |
| Acceptance | Exact displayed text, correct suffix and caret, one undo, stale/read-only rejection |
| Requests | Default and configured debounce/deadline, automatic trigger gates/manual bypass, cancellation and late/out-of-order response rejection |
| Locality | Remote URL rejection, loopback normalization, no proxy or redirect escape |
| Failures | Disconnected server, missing model, malformed/oversized output, empty response and timeout |
| Settings | Persistence, defaults, disabled path, test without source code, changes invalidate active work |
| Regression | Existing Ctrl+Space/popup/Tab/indentation and other editor types retain behavior |
| Models | Separate live smoke tests on Ollama and LM Studio with actual Qwen Base model; record runtime/model versions and latency |
| Build | Compile affected Qt classes and tests; build/install OMEdit when its full dependency toolchain is available |

Use deterministic fake HTTP responses for correctness tests. Real model quality and CPU latency are separate evaluations on declarations, equations, connect statements, annotations, and multiline snippets. Passing transport tests does not establish Modelica quality or live runtime compatibility.

## Non-goals

Remote/LAN/cloud endpoints, model downloads, server startup flags, runtime installation, chat, agent actions, project retrieval, compiler verification on each suggestion, partial acceptance, suggestion cycling, and OMEdit-specific acceleration code are excluded from this version.

## References

- [OMEdit editor](../OMEdit/OMEditLIB/Editors/BaseEditor.cpp)
- [OMEdit Modelica completion](../OMEdit/OMEditLIB/Editors/ModelicaEditor.cpp)
- [Qt plain text layout](https://doc.qt.io/qt-6/qplaintextdocumentlayout.html)
- [VS Code ghost view reference at e504380](https://github.com/microsoft/vscode/blob/e504380fb6fd2c9fecd4576f885d27077d64b903/src/vs/editor/contrib/inlineCompletions/browser/view/ghostText/ghostTextView.ts)
- [VS Code MIT license at the same revision](https://github.com/microsoft/vscode/blob/e504380fb6fd2c9fecd4576f885d27077d64b903/LICENSE.txt)
- [Ollama completion compatibility](https://docs.ollama.com/api/openai-compatibility)
- [LM Studio text completions](https://lmstudio.ai/docs/developer/openai-compat/completions)
- [Qwen2.5 Coder Base](https://huggingface.co/Qwen/Qwen2.5-Coder-1.5B)
