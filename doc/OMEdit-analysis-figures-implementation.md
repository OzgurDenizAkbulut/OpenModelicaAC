# OMEdit Analysis Figures implementation plan

Stage 1 is implemented and validated in the Windows development candidate.
See the [validation report](OMEdit-analysis-figures-validation.md) for measured
results and platform limits, and the [usage guide](OMEdit-analysis-figures.md)
for the user workflow. Python scripting remains stage 2.

## Outcome and architecture

Implement the agreed stage-1 no-code 2D/3D workspace as an additional MDI window
in the Plotting perspective, with View > Windows and Plot toolbar actions.
Existing OMPlot and physical animation remain available.

Use the existing C++ result reader and Qt widgets for the builder and persistence.
Render with a separate managed Python process using Matplotlib WebAgg in a
QWebEngineView. This avoids sharing Python Qt widgets with the C++ application.
Use loopback-only HTTP/WebSockets and a per-process secret. No arbitrary Python
execution endpoint is exposed. Python scripting is stage 2.

Preserve each series' original sample times and full-resolution export data.
Missing variables and invalid surfaces produce actionable panel errors.
Pinning copies result data before the next simulation can overwrite it.

## Parallel ownership

Four GPT-6.1 Sol medium coding lanes share the following contract and own disjoint
files. The coordinating agent owns this plan, review, build execution and final
verification. Agents do not commit, reset, switch branches or run full builds.

1. Renderer: `OMEdit/Resources/analysis/` Python worker and renderer tests.
2. Builder: `OMEdit/OMEditLIB/Plotting/Analysis/AnalysisFigureWindow.*` and
   `AnalysisWorkerClient.*`; Qt no-code UI and worker lifecycle.
3. Data: `OMEdit/OMEditLIB/Plotting/Analysis/AnalysisFigureData.*` and
   `OMEdit/Testsuite/AnalysisFigures/AnalysisFigureDataTest.cpp`; result reading,
   schema validation, saving, pinning, portable packages and CSV.
4. Integration/deployment: MainWindow, PlotWindowContainer, VariablesWidget,
   CMake registration, optional managed runtime packaging and test CMake.

## Milestones

1. Agree the wire/data contract below and implement the four lanes.
2. Integrate a real result file from Variables Browser into a no-code figure.
3. Verify every chart type, surface shape errors, units, playback and missing data.
4. Verify save/reopen, pinned data survival, portable relocation and exports.
5. Build/install OMEdit using the existing development wrapper, then run focused
   tests separately from compilation. Inspect the running UI where available.
6. Measure the agreed 10 x 100,000 sample and 100 x 100 surface cases. Record
   actual limits and unverified platform coverage; do not claim unmeasured speed.

## Shared version-1 definition

All keys below are JSON. Unknown optional keys may be preserved. Reject unknown
versions. Paths are absolute in memory and resolved relative to the definition
file when saved/opened. Source IDs are stable strings.

```
{version:1, title:"Analysis Figure", rows:1, columns:1,
 width:10, height:6, dpi:120, fontSize:10, background:"white",
 sources:[{id:"run1", path:"...", pinned:false, originalPath:"..."}],
 panels:[{id:"panel1", type:"time", title:"", xLabel:"", yLabel:"", zLabel:"",
   legend:true, grid:true, logX:false, logY:false, linkX:"",
   series:[{source:"run1", x:"time", y:"a", z:"", color:"", label:"a",
     xUnit:"s", yUnit:"", zUnit:"", xScale:1, xOffset:0,
     yScale:1, yOffset:0, zScale:1, zOffset:0}],
   surface:{source:"run1", z:"temperature", x:"", y:""}}]}
```

Types: `time`, `xy`, `scatter`, `histogram`, `heatmap`, `trajectory3d`,
`scatter3d`, `surface`. Heatmap and surface use the `surface` object. Rectangular
array variables are flattened result names such as `temperature[1,1]`.
Playback samples arrays at the last stored sample at or before the requested
time, preserving event ordering; it does not interpolate. Missing coordinate
arrays use one-based indices. Surface rows correspond to first Z index (Y),
columns to second Z index (X). Coordinates must be 1D with matching lengths.

## C++ data contract

Namespace `AnalysisFigures`, class `AnalysisFigureData` with static methods:

```
QJsonObject defaultDefinition();
QStringList variables(const QString &path, QString *error = nullptr);
QJsonObject renderPayload(const QJsonObject &definition, double time,
                          QStringList *errors = nullptr);
bool save(const QJsonObject &definition, const QString &file, QString *error = nullptr);
QJsonObject load(const QString &file, QString *error = nullptr);
bool pinSource(QJsonObject &definition, const QString &sourceId,
               const QString &storageDirectory, QString *error = nullptr);
bool exportPackage(const QJsonObject &definition, const QString &directory,
                   QString *error = nullptr);
bool exportCsv(const QJsonObject &definition, const QString &file,
               QString *error = nullptr);
```

Portable package is a new directory containing `figure.omfigure` plus `data/`;
it reopens through normal load. Do not overwrite unrelated files. JSON saves
use QSaveFile. No transformations requiring arbitrary code are supported.
CSV is long form with panel/series/source/variable/time/value identifiers so
unmatched timestamps do not force alignment. Include surface cells if possible.

`renderPayload` copies figure/panel appearance, replaces each series' variable
names with numeric arrays `x`, `y`, `z`, `color`, and adds original `time` arrays.
It emits resolved unit labels and applies explicitly provided affine conversions.
Panels that fail resolution include `error`. Surface payload has numeric 1D
`x`, `y`, 2D `z`, and axis labels. Payload includes `time` for playback cursor.
For playback, curves remain complete and show a current-time marker; surfaces
are sampled. Render payload retains full data for export.

## Renderer protocol

Start `analysis_worker.py --token SECRET --port 0`. Bind 127.0.0.1 only.
Write one flushed JSON startup line to stdout:
`{"event":"ready","url":"http://127.0.0.1:PORT/?token=SECRET"}`.
Diagnostics go to stderr. Requests use newline-delimited JSON on stdin and
responses on stdout, correlated by `id`:

- `{id,op:"render",figure:PAYLOAD}` -> `{id,ok:true}` or `{id,ok:false,error}`.
- `{id,op:"export",format:"png|svg|pdf",path:"..."}` -> success/error.
- `{id,op:"shutdown"}` -> exit. EOF also shuts down.

Run stdin reading outside the Tornado event loop, schedule render/export on it.
Serve Matplotlib's WebAgg HTML/JS and authenticated WebSocket handlers. Keep one
figure per process/window and maintain its manager across redraws. Render errors
must not kill the process. Exports use full data and requested size/dpi/style.
No external network resources are required for the page.

## UI integration contract

`AnalysisFigureWindow : QWidget` lives in namespace `AnalysisFigures`:

```
explicit AnalysisFigureWindow(QWidget *parent = nullptr);
void addVariable(const QString &resultFile, const QString &variable,
                 const QString &unit = QString(), const QString &displayUnit = QString());
void refreshResult(const QString &resultFile);
```

It owns its definition, data adapter calls, worker client, and web view. It has
source/panel/series selection, editable X/Y/Z/color assignments, grid layout,
labels/style/log/legend controls, save/open/pin/package/CSV/figure export,
export preview and a playback slider/play-pause. Use bounded startup timeouts
and stop/kill the worker on destruction. Provide clear unavailable-runtime UI.

Variables Browser drag payload MIME `application/x-omedit-analysis-variable`
is JSON `{file,variable,unit,displayUnit}`. A context action can also call
`addVariable`; dropping onto a field assigns that field and source.
Integration calls `refreshResult` after result insertion/replacement. Menu
invocation switches to Plotting and creates/activates the new analysis window.

Runtime resolution: optional developer environment `OMEDIT_ANALYSIS_PYTHON`,
then installed `share/omedit/analysis/runtime/python.exe` (Windows) or
`share/omedit/analysis/runtime/bin/python3` (Unix). Worker path is installed
`share/omedit/analysis/analysis_worker.py`; build configuration supplies source
fallback for development. Ordinary users use the managed installed runtime.
Packaging accepts a pre-provisioned runtime directory and verifies its required
imports. It never installs packages on application startup.

## Validation ownership

Renderer lane: Python unit/integration tests for charts, WebAgg auth/events,
exports, failure recovery, shutdown and benchmark script.
Data lane: Qt tests with tiny CSV fixtures for timestamps, missing variables,
arrays, pin-copy behavior and package relocation.
Integration lane: register tests and provide deployment staging validation.
Coordinator: serialize builds and tests, inspect cross-lane interfaces, verify
installed feature and document any remaining platform/interactive limitations.
