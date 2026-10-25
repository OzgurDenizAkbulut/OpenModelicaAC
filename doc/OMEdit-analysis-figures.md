# Analysis figures in OMEdit

Analysis figures provide an optional no-code workspace for scientific 2D and 3D
plots. Open **View > Windows > New Analysis Figure**, or use the new action on
the Plot toolbar. The window opens inside the Plotting perspective.

The optional analysis runtime must be included in the OMEdit installation.
Ordinary users do not need to configure Python. Distribution builders can use
the [runtime packaging guide](OMEdit-analysis-runtime-packaging.md).

## Create a figure

1. Simulate a model or open a result file in OMEdit.
2. Right-click a numeric result variable and choose **Add to Analysis Figure**,
   or drag it from Variables Browser into a labeled variable field.
3. Select a plot type and assign its variables. Time plots use simulation time;
   XY and 3D plots use the explicitly assigned X, Y and Z variables.
4. Add series to compare curves or add panels to arrange several plots in a grid.
5. Edit titles, labels, units, logarithmic axes, legends and the figure size.

Inside a figure, **Add result…** opens a result and makes its variables available
in the Variables Browser. **Variables…** lists the selected source's columns;
select one and use **Assign to Y / surface Z**, or drag browser variables into
the X, Y and Z fields. CSV result files need a header of variable names and
numeric sample rows; quote array names containing commas, such as
`"temperature[1,1]"`.

The first release includes time series, XY lines, scatter, histograms, heatmaps,
3D trajectories, 3D scatter and rectangular surfaces. The Color field can color
scatter points by another variable. Variables within one series belong to one
result source; comparisons across runs use separate series.

Use a matching **Link X group** on compatible panels to link their X axes.
Incompatible units or quantities require separate panels. Unit conversion uses
OMEdit's unit conversion support. Missing variables appear as unresolved panels
so they can be reassigned after a model change.

## Surfaces and playback

For a surface or heatmap, assign a rectangular array base name to Z. For example,
`temperature[1,1]` through `temperature[10,20]` form a 10-row, 20-column array.
The first index maps to Y and the second to X.

Optional X and Y coordinate arrays must have 20 and 10 elements respectively in
that example. Leave them blank to use clearly labeled one-based column and row
indices. Ragged arrays and mismatched coordinate lengths are rejected.

The playback slider selects stored simulation time. Curves retain their original
samples and display a current-time marker. Surfaces use the last stored sample
at or before that time, including the last sample at duplicate event times.
Playback does not interpolate between runs or run a live simulation.

Large figures may display reduced detail to keep navigation practical. The
figure indicates when that occurs. Analysis data, CSV and image exports retain
the full data.

## Save, compare and share

**Save** writes an editable `.omfigure` definition with references to result
files. Keep those files available when reopening it. Unpinned results refresh
when OMEdit reloads a simulation result at the same path.

Use **Pin run** before re-simulating to retain a copy of a result that might be
overwritten. The selected source becomes pinned; add the new run as a separate
source or series to compare it with the retained run.

**Package** creates a new directory containing `figure.omfigure` and copied
result data. Move or share the whole directory and open its `figure.omfigure`.
It does not require the original machine's result paths.

**CSV** exports selected series data with source, variable, axis, unit and native
timestamps. Different runs remain separate; no implicit resampling occurs.

## Export

Set width, height, DPI, background and font size, inspect **Preview**, then use
**Export** for PNG, SVG or PDF. Full-resolution export can take longer than
interactive drawing. Matplotlib's depth-ordering limitations still apply to
intersecting 3D objects; this feature is intended for scientific plots rather
than physical scene rendering.

Python scripting is reserved for stage 2. Irregular meshes, imported tables,
automated sweeps, FFTs and fitting are outside this initial implementation.
