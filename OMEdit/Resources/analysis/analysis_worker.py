"""Managed, loopback-only stage-1 analysis renderer. Commands arrive on stdin."""

import argparse
import base64
import copy
import hmac
import io
import json
import logging
import math
from pathlib import Path
import signal
import sys
import threading
from urllib.parse import urlsplit

import matplotlib
matplotlib.use("WebAgg")
from matplotlib.backends.backend_webagg_core import new_figure_manager_given_figure
from matplotlib.figure import Figure
import numpy as np
import tornado.httpserver
import tornado.ioloop
import tornado.netutil
import tornado.web
import tornado.websocket


TYPES = {"time", "xy", "scatter", "histogram", "heatmap",
         "trajectory3d", "scatter3d", "surface"}
FORMATS = {"png": "image/png", "svg": "image/svg+xml", "pdf": "application/pdf"}
WEBAGG_FRAME_QUEUE = r'''
function analysisFrameQueue(fig, afterMessage) {
  const receive=fig.ws.onmessage, paint=fig.imageObj.onload, acknowledge=fig.updated_canvas_event;
  const pending=[];
  let decoding=false;
  function drain() {
    while (!decoding && pending.length) {
      const event=pending.shift();
      const image=event.data instanceof Blob || (typeof event.data==='string' && event.data.startsWith('data:image/png;base64'));
      decoding=image;
      receive.call(fig.ws,event);
      if (image) fig.waiting=true;
      else afterMessage(event);
    }
  }
  // WebAgg's diff frames and image-mode/resize messages form one ordered stream.
  fig.updated_canvas_event=function() {};
  fig.imageObj.onload=function() {
    paint.apply(this,arguments);
    decoding=false;
    fig.waiting=false;
    acknowledge.call(fig);
    drain();
  };
  fig.imageObj.onerror=function() {
    fig.ws.close();
    document.getElementById('mpl-warnings').textContent='The analysis frame could not be decoded. Reopen the figure to reconnect.';
  };
  fig.ws.onmessage=function(event) { pending.push(event); drain(); };
}
'''


def vector(value, name):
    result = np.asarray(value, dtype=float)
    if result.ndim != 1 or not result.size:
        raise ValueError(f"{name} must be a nonempty one-dimensional array")
    return result


def positive(value, name, maximum):
    value = float(value)
    if not math.isfinite(value) or not 0 < value <= maximum:
        raise ValueError(f"{name} must be positive and at most {maximum}")
    return value


def envelope_indices(y, limit=4000):
    """Keep bucket endpoints, extrema and gaps in their original order."""
    if len(y) <= limit:
        return np.arange(len(y))
    count = max(1, limit // 5)
    size = math.ceil(len(y) / count)
    indices = []
    for start in range(0, len(y), size):
        stop = min(start + size, len(y))
        bucket = y[start:stop]
        finite = np.flatnonzero(np.isfinite(bucket))
        indices.extend((start, stop - 1))
        if finite.size:
            indices.extend((start + finite[np.argmin(bucket[finite])], start + finite[np.argmax(bucket[finite])]))
        gaps = np.flatnonzero(~np.isfinite(bucket))
        if gaps.size:
            indices.append(start + gaps[0])
    finite = np.isfinite(y)
    transitions = np.flatnonzero(finite[1:] != finite[:-1])
    return np.unique(np.concatenate((np.asarray(indices, dtype=int), transitions, transitions + 1)))


def panel_signature(panel):
    series = [[s.get(key) for key in ("source", "xVariable", "yVariable", "zVariable", "xUnit", "yUnit", "zUnit")] for s in panel.get("series", [])]
    surface = panel.get("surface", {})
    return json.dumps([panel.get("type", "time"), panel.get("logX"), panel.get("logY"), series,
                       [surface.get(key) for key in ("source", "xVariable", "yVariable", "zVariable")]], sort_keys=True)


class Renderer:
    def __init__(self, full_detail=False):
        self.figure = Figure(figsize=(10, 6), dpi=120)
        self.manager = new_figure_manager_given_figure(1, self.figure)
        if self.manager.toolbar is None:
            self.manager.toolbar = self.manager.ToolbarCls(self.manager.canvas)
        self.payload = None
        self.panel_errors = []
        self.panel_axes = {}
        self.panel_images = {}
        self.panel_colorbars = {}
        self.full_detail = full_detail
        self.reduced = False
        self.panel_signatures = {}
        self.layout_options = {}
        self.manager.canvas.mpl_connect("resize_event", self._resize_layout)
        self.manager.canvas.mpl_connect("draw_event", self._freeze_layout)

    def _resize_layout(self, event):
        if self.layout_options:
            self.figure.set_layout_engine("constrained", **self.layout_options)

    def _freeze_layout(self, event):
        # A rotating 3D bounding box must not move the axes under the pointer.
        if not self.full_detail:
            self.figure.set_layout_engine("none")

    def _update_playback(self, payload):
        if self.payload is None or self.panel_errors:
            return False
        def assignments(value):
            result = {key: item for key, item in value.items() if key not in {"time", "times", "panels"}}
            result["panels"] = [{**panel, "surface": {key: item for key, item in panel.get("surface", {}).items() if key != "z"}}
                                for panel in value.get("panels", [])]
            return result
        previous, current = assignments(self.payload), assignments(payload)
        if previous != current:
            return False
        for index, panel in enumerate(payload.get("panels", [])):
            kind = panel.get("type", "time")
            if kind == "histogram":
                continue
            ax = self.panel_axes[panel.get("id", str(index))]
            limits = [(axis, getattr(ax, f"get_{axis}lim")(), getattr(ax, f"get_autoscale{axis}_on")())
                      for axis in ("xyz" if hasattr(ax, "get_zlim") else "xy")]
            if kind in {"surface", "heatmap"}:
                if panel.get("surface") != self.payload["panels"][index].get("surface"):
                    try:
                        image = self.surface_image(ax, panel, replacing=self.panel_images[ax])
                    except (ValueError, TypeError, KeyError, IndexError, RuntimeError):
                        return False
                    self.panel_images[ax] = image
                    self.panel_colorbars[ax].update_normal(image)
            else:
                for line in list(ax.lines):
                    if line.get_label() in {"_playback", "_playback_time"}:
                        line.remove()
                self.draw_playback(ax, panel, payload.get("time"))
            for axis, bounds, auto in limits:
                if kind not in {"surface", "heatmap"} or not auto:
                    getattr(ax, f"set_{axis}lim")(bounds, auto=auto)
        self.payload = copy.deepcopy(payload)
        return True

    def render(self, payload, draw=True):
        if payload.get("version", 1) != 1:
            raise ValueError("Unsupported analysis figure version")
        rows, columns = int(payload.get("rows", 1)), int(payload.get("columns", 1))
        if not 1 <= rows <= 16 or not 1 <= columns <= 16:
            raise ValueError("Grid dimensions must be between 1 and 16")
        panels = payload.get("panels", [])
        if payload.get("error"):
            panels = [{"id": "figure-error", "error": payload["error"]}]
        if len(panels) > rows * columns:
            raise ValueError("The grid has fewer cells than panels")
        width = positive(payload.get("width", 10), "Width", 100)
        height = positive(payload.get("height", 6), "Height", 100)
        dpi = positive(payload.get("dpi", 120), "Resolution", 1200)
        if width * height * dpi * dpi > 100_000_000:
            raise ValueError("Figure exceeds 100 million pixels; reduce size or resolution")
        font = positive(payload.get("fontSize", 10), "Font size", 100)
        background = payload.get("background", "white")
        matplotlib.colors.to_rgba(background)
        if self._update_playback(payload):
            if draw:
                self.manager.canvas.draw()
            return self.panel_errors
        views = {key: (ax.elev, ax.azim, ax.roll) for key, ax in self.panel_axes.items() if hasattr(ax, "elev")}
        limits = {key: [(axis, getattr(ax, f"get_{axis}lim")(), getattr(ax, f"get_autoscale{axis}_on")())
                        for axis in ("xyz" if hasattr(ax, "get_zlim") else "xy")]
                  for key, ax in self.panel_axes.items()}
        signatures = self.panel_signatures
        self.panel_signatures = {}
        self.figure.clear()
        self.panel_axes = {}
        self.panel_images = {}
        self.panel_colorbars = {}
        self.reduced = False
        # Export dimensions belong to the isolated export renderer. The live
        # canvas belongs to WebAgg's viewport and device-pixel-ratio handshake.
        if self.full_detail:
            self.figure.set_size_inches(width, height, forward=False)
            self.figure.set_dpi(dpi)
        self.figure.set_facecolor(background)
        has_3d = any(panel.get("type") in {"trajectory3d", "scatter3d", "surface"} for panel in panels)
        self.layout_options = dict(w_pad=.45 / columns if has_3d else .06,
                                   h_pad=.18 / rows if has_3d else .06, hspace=.15, wspace=.15)
        self.figure.set_layout_engine("constrained", **self.layout_options)
        self.panel_errors = []
        links = {}
        with matplotlib.rc_context({"font.size": font}):
            self.figure.suptitle(payload.get("title", "Analysis Figure"))
            for index, panel in enumerate(panels):
                kind = panel.get("type", "time")
                ax = self.figure.add_subplot(rows, columns, index + 1,
                                             projection="3d" if kind in {"trajectory3d", "scatter3d", "surface"} else None)
                panel_id = panel.get("id", str(index))
                self.panel_axes[panel_id] = ax
                signature = panel_signature(panel)
                self.panel_signatures[panel_id] = signature
                try:
                    if panel.get("error"):
                        raise ValueError(panel["error"])
                    self.draw_panel(ax, panel, payload.get("time"))
                    same_assignment = signatures.get(panel_id) == signature
                    if same_assignment and panel_id in views and hasattr(ax, "view_init"):
                        ax.view_init(*views[panel_id])
                    if same_assignment and panel_id in limits:
                        for axis, bounds, auto in limits[panel_id]:
                            if not auto:
                                getattr(ax, f"set_{axis}lim")(bounds)
                    group = panel.get("linkX")
                    series = panel.get("series", [])
                    if group and series and kind in {"time", "xy", "scatter"}:
                        identities = {(s.get("xVariable", "time" if kind == "time" else ""), s.get("xUnit", "")) for s in series}
                        if len(identities) == 1 and next(iter(identities))[0]:
                            key = (group, next(iter(identities)))
                            if key in links:
                                ax.sharex(links[key])
                            else:
                                links[key] = ax
                except (ValueError, TypeError, KeyError, IndexError, RuntimeError) as error:
                    self.panel_errors.append({"panel": panel.get("id", str(index)), "error": str(error)})
                    ax.clear()
                    message = "Unresolved panel\n" + str(error)
                    if hasattr(ax, "text2D"):
                        ax.text2D(.5, .5, message, ha="center", va="center", transform=ax.transAxes, wrap=True)
                    else:
                        ax.text(.5, .5, message, ha="center", va="center", transform=ax.transAxes, wrap=True)
                    ax.set_title(panel.get("title", ""))
            if self.reduced:
                self.figure.text(.01, .01, "Display detail reduced; exports include all samples", fontsize=8, color="dimgray")
        self.payload = copy.deepcopy(payload)
        self.manager.toolbar.update()
        if draw:
            self.manager.canvas.draw()
        return self.panel_errors

    def draw_playback(self, ax, panel, playback):
        kind = panel.get("type", "time")
        if playback is None or kind in {"surface", "heatmap", "histogram"}:
            return
        for item in panel.get("series", []):
            x = item.get("x", item.get("time", []))
            y, z = item.get("y", []), item.get("z", [])
            times = item.get("time", [])
            if len(times) == len(x):
                eligible = np.flatnonzero(np.asarray(times, dtype=float) <= float(playback))
                if eligible.size:
                    i = eligible[-1]
                    args = ([x[i]], [y[i]], [z[i]]) if kind in {"trajectory3d", "scatter3d"} else ([x[i]], [y[i]])
                    ax.plot(*args, "o", color="black", markersize=5, label="_playback")
        if kind == "time":
            first = panel["series"][0]
            position = float(playback) * float(first.get("xScale", 1)) + float(first.get("xOffset", 0))
            ax.axvline(position, color="black", alpha=.4, linestyle="--", label="_playback_time")

    def draw_panel(self, ax, panel, playback):
        kind = panel.get("type", "time")
        if kind not in TYPES:
            raise ValueError(f"Unknown plot type: {kind}")
        if kind in {"surface", "heatmap"}:
            surface = panel.get("surface", {})
            image = self.surface_image(ax, panel)
            self.panel_images[ax] = image
            self.panel_colorbars[ax] = self.figure.colorbar(image, ax=ax, shrink=.7,
                location="bottom" if kind == "surface" else "right", pad=.16 if kind == "surface" else .05,
                label=surface.get("zLabel", panel.get("zLabel", "")))
            x_label = surface.get("xLabel", "Column (1-based)")
            y_label = surface.get("yLabel", "Row (1-based)")
            if x_label == "Column index (1-based)":
                x_label = "Column (1-based)"
            if y_label == "Row index (1-based)":
                y_label = "Row (1-based)"
            ax.set_xlabel(panel.get("xLabel") or x_label)
            ax.set_ylabel(panel.get("yLabel") or y_label)
        else:
            series = panel.get("series", [])
            if not series:
                raise ValueError("Assign at least one result variable")
            for series_index, item in enumerate(series):
                y = vector(item.get("y", []), "Y")
                label = item.get("label") or item.get("yVariable") or item.get("yLabel") or f"Series {series_index + 1}"
                if kind == "histogram":
                    ax.hist(y[np.isfinite(y)], bins=panel.get("bins", 30), label=label, alpha=.65)
                    continue
                x = vector(item.get("x", item.get("time", [])), "X")
                if len(x) != len(y):
                    raise ValueError("X and Y must have matching sample counts; no interpolation is performed")
                z = None
                if kind in {"trajectory3d", "scatter3d"}:
                    z = vector(item.get("z", []), "Z")
                    if len(z) != len(x):
                        raise ValueError("Z must match X and Y sample counts")
                indices = np.arange(len(x))
                if not self.full_detail:
                    if kind in {"time", "xy"}:
                        indices = envelope_indices(y)
                        if kind == "xy":
                            indices = np.union1d(indices, envelope_indices(x))
                    else:
                        cap = 3000 if z is not None else 10000
                        if len(x) > cap:
                            indices = np.linspace(0, len(x) - 1, cap, dtype=int)
                            for values in (x, y, z):
                                if values is not None:
                                    mask = np.isfinite(values)
                                    finite = np.flatnonzero(mask)
                                    if finite.size:
                                        extrema = finite[[np.argmin(values[finite]), np.argmax(values[finite])]]
                                        indices = np.union1d(indices, extrema)
                                    transitions = np.flatnonzero(mask[1:] != mask[:-1])
                                    indices = np.union1d(indices, np.concatenate((transitions, transitions + 1)))
                    self.reduced |= len(indices) < len(x)
                if kind in {"scatter", "scatter3d"}:
                    color = item.get("color", [])
                    kwargs = {"label": label, "s": 12}
                    if isinstance(color, list) and color:
                        color = vector(color, "Color")
                        if len(color) != len(x):
                            raise ValueError("Color must match the sample count")
                        kwargs.update(c=color[indices], cmap="viridis")
                    image = ax.scatter(x[indices], y[indices], **kwargs) if z is None else ax.scatter(x[indices], y[indices], z[indices], **kwargs)
                    if "c" in kwargs:
                        self.figure.colorbar(image, ax=ax, shrink=.7, location="bottom" if z is not None else "right",
                                             pad=.16 if z is not None else .05,
                                             label=item.get("colorLabel", item.get("colorUnit", "")))
                elif z is None:
                    ax.plot(x[indices], y[indices], label=label)
                else:
                    ax.plot(x[indices], y[indices], z[indices], label=label)
            first = series[0]
            if kind == "histogram":
                ax.set_xlabel(panel.get("xLabel") or first.get("yLabel") or first.get("yUnit", ""))
                ax.set_ylabel(panel.get("yLabel") or "Count")
            else:
                ax.set_xlabel(panel.get("xLabel") or first.get("xLabel") or first.get("xUnit", ""))
                ax.set_ylabel(panel.get("yLabel") or first.get("yLabel") or first.get("yUnit", ""))
            if panel.get("legend", True):
                handles, labels = ax.get_legend_handles_labels()
                if labels:
                    ax.legend(handles, labels)
        if hasattr(ax, "set_zlabel"):
            first = panel.get("series", [{}])[0] if panel.get("series") else {}
            ax.set_zlabel(panel.get("zLabel") or panel.get("surface", {}).get("zLabel") or first.get("zLabel") or first.get("zUnit", ""))
        ax.set_title(panel.get("title", ""))
        ax.grid(panel.get("grid", True))
        if panel.get("logX"):
            ax.set_xscale("log")
        if panel.get("logY"):
            ax.set_yscale("log")
        self.draw_playback(ax, panel, playback)

    def surface_image(self, ax, panel, replacing=None):
        surface = panel.get("surface", {})
        z = np.asarray(surface.get("z", []), dtype=float)
        if z.ndim != 2 or not z.size:
            raise ValueError("Z must be a nonempty rectangular two-dimensional array")
        y = vector(surface.get("y", list(range(1, z.shape[0] + 1))), "Surface Y")
        x = vector(surface.get("x", list(range(1, z.shape[1] + 1))), "Surface X")
        if z.shape != (len(y), len(x)):
            raise ValueError("Z rows must match Y and Z columns must match X")
        if not np.isfinite(x).all() or not np.isfinite(y).all():
            raise ValueError("Surface coordinates must be finite")
        if replacing is not None:
            replacing.remove()
            ax.relim()
        if panel.get("type") == "surface":
            xx, yy = np.meshgrid(x, y)
            rows = z.shape[0] if self.full_detail else min(35, z.shape[0])
            cols = z.shape[1] if self.full_detail else min(35, z.shape[1])
            self.reduced |= (rows, cols) != z.shape
            return ax.plot_surface(xx, yy, z, cmap="viridis", rcount=rows, ccount=cols)
        return ax.pcolormesh(x, y, z, shading="auto", cmap="viridis")

    def export(self, fmt, target):
        if fmt not in FORMATS:
            raise ValueError("Export format must be png, svg or pdf")
        if self.payload is None:
            raise ValueError("Render a figure before exporting")
        if not self.full_detail:
            complete = Renderer(full_detail=True)
            complete.render(self.payload, draw=False)
            for displayed, exported in zip(self.figure.axes, complete.figure.axes):
                exported.set_xlim(displayed.get_xlim())
                exported.set_ylim(displayed.get_ylim())
                if hasattr(displayed, "elev"):
                    exported.set_zlim(displayed.get_zlim())
                    exported.view_init(displayed.elev, displayed.azim, displayed.roll)
            complete.export(fmt, target)
            return
        # Browser resize events must not change the requested export dimensions.
        size = self.figure.get_size_inches().copy()
        try:
            self.figure.set_size_inches(self.payload.get("width", 10), self.payload.get("height", 6), forward=False)
            with matplotlib.rc_context({"path.simplify": False}):
                self.figure.savefig(target, format=fmt, dpi=self.payload.get("dpi", 120),
                                    facecolor=self.payload.get("background", "white"))
        finally:
            self.figure.set_size_inches(size, forward=False)


class Authentication:
    def prepare(self):
        if self.request.host != self.application.host:
            raise tornado.web.HTTPError(403)
        supplied = self.get_query_argument("token", None) or self.get_cookie(self.application.cookie_name, "")
        if not hmac.compare_digest(supplied, self.application.token):
            raise tornado.web.HTTPError(403)

    def set_default_headers(self):
        self.set_header("Cache-Control", "no-store")
        self.set_header("X-Content-Type-Options", "nosniff")
        self.set_header("Referrer-Policy", "no-referrer")
        self.set_header("Content-Security-Policy", "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; img-src 'self' blob: data:; connect-src 'self'; frame-ancestors 'none'")


class MainPage(Authentication, tornado.web.RequestHandler):
    def get(self):
        self.set_cookie(self.application.cookie_name, self.application.token, httponly=True, samesite="Strict")
        self.write('''<!doctype html><html><head><meta charset="utf-8">
<link rel="stylesheet" href="/_static/css/mpl.css"><script src="/mpl.js"></script>
<style>html,body{margin:0;background:white;width:100%;height:100%;overflow:hidden}#figure{display:block;width:100%;height:100%;min-width:0;overflow:hidden}
.analysis-canvas-container{width:calc(100% - 2px)!important;max-width:calc(100% - 2px)!important;min-width:0!important;height:0!important;min-height:1px!important;flex:1 1 0!important;resize:none!important}
</style></head>
<body><div id="mpl-warnings"></div><div id="figure"></div><script>
const token=new URLSearchParams(location.search).get('token');
const socket=new WebSocket('ws://'+location.host+'/ws?token='+encodeURIComponent(token));
const fig=new mpl.figure(1,socket,function(){document.getElementById('mpl-warnings').textContent='Use Export in the analysis window to save a figure.';},document.getElementById('figure'));
fig.root.style.cssText='display:flex;flex-direction:column;width:100%;max-width:100%;min-width:0;height:100%;box-sizing:border-box;overflow:hidden;';
fig.canvas_div.classList.add('analysis-canvas-container');
fig.root.querySelector('.mpl-toolbar').style.flex='0 0 auto';
document.documentElement.dataset.analysisReady='false';
const loaded=fig.imageObj.onload;
fig.imageObj.onload=function(){loaded.apply(this,arguments);document.documentElement.dataset.analysisReady=String(fig.imageObj.naturalWidth===fig.canvas.width&&fig.imageObj.naturalHeight===fig.canvas.height);};
function viewport(){return [Math.max(1,fig.canvas_div.clientWidth),Math.max(1,fig.canvas_div.clientHeight)];}
let resizeTimer=0,lastSize='';
function fit(force){clearTimeout(resizeTimer);resizeTimer=setTimeout(function(){if(socket.readyState===1){const size=viewport(),key=size.join('x');if(force===true||key!==lastSize){lastSize=key;document.documentElement.dataset.analysisReady='false';fig.request_resize(size[0],size[1]);}}},0);}
window.addEventListener('resize',function(){fit(false);});
new ResizeObserver(function(){fit(false);}).observe(fig.canvas_div);
socket.addEventListener('open',function(){fit(true);});
''' + WEBAGG_FRAME_QUEUE + '''
analysisFrameQueue(fig,function(event){if(typeof event.data==='string'&&event.data.startsWith('{')){const message=JSON.parse(event.data);if(message.type==='resize'){const size=viewport();if(Math.abs(message.size[0]-size[0])>1||Math.abs(message.size[1]-size[1])>1)fit(true);}}});
</script></body></html>''')


class Javascript(Authentication, tornado.web.RequestHandler):
    def get(self):
        self.set_header("Content-Type", "application/javascript")
        self.write(self.application.renderer.manager.get_javascript())
        self.write('\nmpl.extensions = ["png", "svg", "pdf"]; mpl.default_extension = "png";\n')
        self.write('mpl.toolbar_items = mpl.toolbar_items.filter(function(item){return item[3] !== "download";});\n')


class Static(Authentication, tornado.web.StaticFileHandler):
    pass


class Download(Authentication, tornado.web.RequestHandler):
    def get(self, fmt):
        if fmt not in FORMATS:
            raise tornado.web.HTTPError(400, "Unsupported format")
        data = io.BytesIO()
        self.application.renderer.export(fmt, data)
        self.set_header("Content-Type", FORMATS[fmt])
        self.set_header("Content-Disposition", f'attachment; filename="figure.{fmt}"')
        self.write(data.getvalue())


class WebSocket(Authentication, tornado.websocket.WebSocketHandler):
    supports_binary = True
    registered = False

    def check_origin(self, origin):
        parsed = urlsplit(origin)
        return parsed.scheme == "http" and parsed.netloc == self.application.host

    def open(self):
        self.application.renderer.manager.add_web_socket(self)
        self.registered = True
        self.set_nodelay(True)

    def on_close(self):
        if self.registered:
            self.application.renderer.manager.remove_web_socket(self)

    def on_message(self, message):
        try:
            event = json.loads(message)
            if not isinstance(event, dict):
                raise ValueError("Expected object")
            kind = event.get("type")
            if kind == "supports_binary":
                self.supports_binary = bool(event.get("value"))
            elif kind in {"ack", "draw", "refresh", "resize", "send_image_mode", "set_device_pixel_ratio", "set_dpi_ratio", "button_press", "button_release", "motion_notify", "figure_enter", "figure_leave", "scroll", "key_press", "key_release", "toolbar_button"}:
                if kind == "toolbar_button" and event.get("name") not in {"home", "back", "forward", "pan", "zoom", "download"}:
                    raise ValueError("Unknown toolbar action")
                if kind == "resize":
                    if not 1 <= float(event.get("width", 0)) <= 8192 or not 1 <= float(event.get("height", 0)) <= 8192:
                        raise ValueError("Invalid canvas size")
                if kind in {"set_device_pixel_ratio", "set_dpi_ratio"}:
                    ratio = float(event.get("device_pixel_ratio", event.get("dpi_ratio", 1)))
                    if not .25 <= ratio <= 4:
                        raise ValueError("Invalid device pixel ratio")
                self.application.renderer.manager.handle_json(event)
            else:
                raise ValueError("Unknown figure event")
        except (ValueError, TypeError, KeyError, AttributeError, RuntimeError):
            self.send_json({"type": "message", "message": "Invalid figure interaction"})

    def send_json(self, content):
        self.write_message(json.dumps(content))

    def send_binary(self, blob):
        if self.supports_binary:
            self.write_message(blob, binary=True)
        else:
            self.write_message("data:image/png;base64," + base64.b64encode(blob).decode("ascii"))


class Application(tornado.web.Application):
    def __init__(self, token):
        self.token = token
        self.host = ""
        self.cookie_name = "analysis_token"
        self.renderer = Renderer()
        static = self.renderer.manager.get_static_file_path()
        super().__init__([
            (r"/", MainPage), (r"/mpl.js", Javascript), (r"/ws", WebSocket),
            (r"/download/(png|svg|pdf)", Download),
            (r"/_static/(.*)", Static, {"path": static}),
            (r"/_images/(.*)", Static, {"path": str(Path(matplotlib.get_data_path()) / "images")}),
        ], websocket_max_message_size=1_000_000, log_function=lambda handler: None)


def respond(value):
    print(json.dumps(value, allow_nan=False), flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--token", required=True)
    parser.add_argument("--port", type=int, default=0)
    args = parser.parse_args()
    if not args.token:
        parser.error("A nonempty session token is required")
    app = Application(args.token)
    loop = tornado.ioloop.IOLoop.current()
    sockets = tornado.netutil.bind_sockets(args.port, address="127.0.0.1")
    server = tornado.httpserver.HTTPServer(app, max_buffer_size=2_000_000)
    server.add_sockets(sockets)
    app.host = f"127.0.0.1:{sockets[0].getsockname()[1]}"
    app.cookie_name = f"analysis_token_{sockets[0].getsockname()[1]}"
    stopping = False

    def stop():
        nonlocal stopping
        if not stopping:
            stopping = True
            loop.stop()

    def dispatch(line):
        request = {}
        try:
            request = json.loads(line)
            if not isinstance(request, dict):
                request = {}
                raise ValueError("Command must be an object")
            operation = request.get("op")
            extra = {}
            if operation == "render":
                extra["errors"] = app.renderer.render(request["figure"])
                if extra["errors"]:
                    respond({"id": request.get("id"), "ok": False,
                             "error": "; ".join(f"{item['panel']}: {item['error']}" for item in extra["errors"]), **extra})
                    return
            elif operation == "export":
                app.renderer.export(request["format"], request["path"])
            elif operation == "shutdown":
                stop()
            else:
                raise ValueError("Unknown analysis operation")
            respond({"id": request.get("id"), "ok": True, **extra})
        except Exception as error:
            logging.exception("Analysis command failed")
            respond({"id": request.get("id"), "ok": False, "error": str(error)})

    def read_commands():
        for line in sys.stdin:
            loop.add_callback(dispatch, line)
        loop.add_callback(stop)

    threading.Thread(target=read_commands, daemon=True).start()
    for sig in (signal.SIGINT, signal.SIGTERM):
        signal.signal(sig, lambda *_: loop.add_callback_from_signal(stop))
    respond({"event": "ready", "url": f"http://{app.host}/?token={args.token}"})
    try:
        loop.start()
    finally:
        stopping = True
        for ws in list(app.renderer.manager.web_sockets):
            ws.close()
        server.stop()
        loop.run_sync(server.close_all_connections, timeout=3)
        loop.close(all_fds=True)


if __name__ == "__main__":
    main()
