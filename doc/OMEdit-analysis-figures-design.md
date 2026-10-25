# OMEdit analysis figures: design discussion

Status: requirements agreed; implementation authorized on 2026-10-25.

## Agreed scope

- Add an optional analysis window within the existing Plotting workspace.
- Make the window available through the existing Windows menu.
- Stage 1 provides graphical figure creation and editing without code.
- Stage 2 adds Python scripting.
- Initially support completed simulation results and playback of stored results.
- The first 3D scope is scientific XYZ trajectories, scatter plots and surfaces.
- Use a figure builder: select a plot type and assign variables to labeled fields,
  including X, Y, Z and color where applicable, using the Variables Browser.
- Save editable figure definitions, not just exported images.
- Figures refresh after re-simulation by default. Users may pin a run for
  comparison; pinning retains the actual result data.
- Initial data sources are single-run variables for trajectories/scatter and
  array-valued results for surfaces. Combining sweep runs, imported tables and
  automated sweep execution are deferred.
- The initial 2D toolbox includes time-series lines, XY plots, scatter,
  histograms and heatmaps, with logarithmic axes, labels, legends and unit
  selection. Specialized analysis such as FFT and curve fitting is deferred.
- A figure can contain mixed 2D and 3D panels in a row-and-column grid. Panels
  share playback time; axes link only when quantities and units are compatible.
- Initial benchmark targets are 10 curves of 100,000 samples each and a
  100 by 100 surface, with responsive navigation on the user's workstation.
  Measurements are recorded in the validation report. Reduced detail during interaction is
  allowed, while full data is retained for analysis and export.
- Normal saves retain editable definitions and result references. Portable
  figure packages include definitions and required data.
- Preserve original timestamps, convert only compatible units, never silently
  align or interpolate runs, and mark missing-variable panels unresolved.
- Surfaces use rectangular Z arrays with X/Y coordinate arrays or explicitly
  labeled array-index coordinates. Validate dimensions; defer irregular meshes.
- Export PNG, SVG and PDF figures and selected plotted data as CSV, with size,
  resolution, background, font-size controls and a preview.
- Stage 1 uses a managed optional plotting runtime; end users need not install
  or configure Python. User scripting and external environments are deferred.

## Source inspection findings

- `OMEdit/OMEditLIB/Plotting/PlotWindowContainer.cpp` hosts plotting windows in
  a `QMdiArea`; a separate analysis window can use this container.
- `OMEdit/OMEditLIB/MainWindow.cpp` explicitly populates View > Windows. A new
  MDI window does not automatically acquire a menu entry.
- `OMEdit/OMEditLIB/Plotting/VariablesWidget.cpp` couples current variable
  selection and unit conversion with OMPlot; integration needs to preserve
  those semantics.
- This checkout's physical animation uses Qt Quick 3D. Its shape/vector scene
  is distinct from scientific plots with axes and surfaces.
- `OMPlot/OMPlot/OMPlotGUI/PlotWindow.cpp` reads results through
  `omc::ResultFile`.

## Implementation decisions

The [implementation plan](OMEdit-analysis-figures-implementation.md) specifies
the completed stage 1 contracts: rectangular arrays use row/Y then column/X
indices, timestamps remain native, compatible unit conversions are explicit,
and missing variables leave an unresolved panel. Editable definitions reference
results; pinning and portable packages copy the actual data.

An isolated Matplotlib WebAgg worker serves the embedded Qt WebEngine view.
The optional managed runtime is staged with the installation. Measured renderer
performance and remaining platform checks are recorded in the
[validation report](OMEdit-analysis-figures-validation.md).

## References

- [Mantid: embedding Matplotlib from C++](https://developer.mantidproject.org/MatplotlibInCpp.html)
- [FreeCAD Plot workbench](https://github.com/FreeCAD/Plot)
- [Matplotlib Qt embedding](https://matplotlib.org/stable/gallery/user_interfaces/embedding_in_qt_sgskip.html)
- [Matplotlib WebAgg embedding](https://matplotlib.org/stable/gallery/user_interfaces/embedding_webagg_sgskip.html)
- [Matplotlib 3D limitations](https://matplotlib.org/stable/api/toolkits/mplot3d/faq.html)
