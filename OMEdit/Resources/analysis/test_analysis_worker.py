"""Run with: python -m unittest discover -s OMEdit/Resources/analysis -v."""

import asyncio
import copy
from http.cookiejar import CookieJar
import io
import json
from pathlib import Path
import queue
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch

import numpy as np
import matplotlib
from urllib.error import HTTPError
from urllib.request import HTTPCookieProcessor, Request, build_opener, urlopen

from tornado.httpclient import HTTPClientError, HTTPRequest
from tornado.websocket import websocket_connect

from analysis_worker import Renderer, WEBAGG_FRAME_QUEUE, envelope_indices


def series():
    return {"x": [0, 1, 1, 2], "y": [1, 2, 3, 4], "z": [4, 3, 2, 1],
            "time": [0, 1, 1, 2], "label": "signal", "xVariable": "time", "xUnit": "s"}


def figure(kind="time"):
    return {"version": 1, "width": 6, "height": 4, "dpi": 80, "time": 1,
            "panels": [{"id": "p", "type": kind, "series": [series()],
                        "surface": {"x": [1, 2], "y": [1, 2, 3], "z": [[1, 2], [3, 4], [5, 6]]}}]}


class RendererTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("node"), "Node.js is needed to exercise the WebAgg image decoder")
    def test_webagg_decodes_frames_and_metadata_in_wire_order(self):
        javascript = (Path(matplotlib.__file__).parent / "backends/web_backend/js/mpl.js").read_text(encoding="utf-8")
        script = "globalThis.window=globalThis;\n" + javascript + "\n" + WEBAGG_FRAME_QUEUE + r'''
const assert = require('node:assert/strict');
const painted = [], sizes = [];
let acknowledgements = 0;
const fig = {imageObj: {src:''}, image_mode:'full', waiting:false,
  updated_canvas_event() { acknowledgements++; },
  handle_image_mode: mpl.figure.prototype.handle_image_mode,
  handle_resize(f, m) { sizes.push(m.size); },
};
fig.imageObj.onload = function() { painted.push([fig.image_mode, this.src]); };
const socket = fig.ws = {onmessage: mpl.figure.prototype._make_on_message_function(fig)};
if (typeof analysisFrameQueue === 'function') analysisFrameQueue(fig, function() {});
socket.onmessage({data:new Blob(['first'])});
const firstSource = fig.imageObj.src;
socket.onmessage({data:JSON.stringify({type:'image_mode',mode:'diff'})});
socket.onmessage({data:new Blob(['second'])});
socket.onmessage({data:JSON.stringify({type:'resize',size:[600,400]})});
assert.equal(fig.imageObj.src, firstSource, 'pending frame must not be overwritten');
assert.equal(fig.image_mode, 'full', 'next frame mode must wait for pending decode');
assert.equal(acknowledgements, 0, 'acknowledge only after image is painted');
assert.equal(sizes.length, 0, 'resize must wait for preceding image');
fig.imageObj.onload();
assert.equal(painted[0][0], 'full');
assert.equal(painted[0][1], firstSource);
assert.notEqual(fig.imageObj.src, firstSource);
assert.equal(acknowledgements, 1);
assert.equal(sizes.length, 0);
fig.imageObj.onload();
assert.equal(painted[1][0], 'diff');
assert.equal(acknowledgements, 2);
assert.deepEqual(sizes, [[600,400]]);
'''
        completed = subprocess.run([shutil.which("node"), "-"], input=script, text=True,
                                   encoding="utf-8", capture_output=True, timeout=10)
        self.assertEqual(completed.returncode, 0, completed.stderr)

    def test_all_eight_charts_and_mixed_grid(self):
        renderer = Renderer()
        payload = figure()
        payload.update(rows=2, columns=4)
        payload["panels"] = [figure(kind)["panels"][0] for kind in
                             ("time", "xy", "scatter", "histogram", "heatmap", "trajectory3d", "scatter3d", "surface")]
        self.assertEqual(renderer.render(payload), [])
        self.assertGreaterEqual(len(renderer.figure.axes), 8)
        for fmt, magic in (("png", b"\x89PNG"), ("svg", b"<?xml"), ("pdf", b"%PDF")):
            data = io.BytesIO()
            renderer.export(fmt, data)
            self.assertTrue(data.getvalue().startswith(magic))

    def test_panel_errors_do_not_hide_valid_panels_and_recover(self):
        renderer = Renderer()
        payload = figure("surface")
        payload["columns"] = 2
        payload["panels"][0]["surface"]["x"] = [1]
        payload["panels"].append(figure()["panels"][0])
        errors = renderer.render(payload)
        self.assertIn("Z rows must match", errors[0]["error"])
        self.assertEqual(len(renderer.figure.axes[1].lines), 3)
        payload["panels"][0] = {"error": "Variable a is missing", "type": "time"}
        self.assertIn("missing", renderer.render(payload)[0]["error"])
        self.assertEqual(renderer.render(figure()), [])

    def test_playback_uses_last_duplicate_event_and_preserves_all_samples(self):
        renderer = Renderer()
        renderer.render(figure())
        lines = renderer.figure.axes[0].lines
        self.assertEqual(list(lines[0].get_xdata()), [0, 1, 1, 2])
        self.assertEqual(list(lines[1].get_ydata()), [3])
        renderer.figure.set_size_inches(2, 2)
        image = io.BytesIO()
        renderer.export("png", image)
        self.assertEqual(struct.unpack(">II", image.getvalue()[16:24]), (480, 320))
        self.assertEqual(list(renderer.figure.get_size_inches()), [2, 2])

    def test_export_never_changes_live_canvas_renderer_or_pixels(self):
        renderer = Renderer()
        renderer.render(figure())
        renderer.manager.canvas.handle_resize({"width": 320, "height": 240})
        renderer.manager.canvas.draw()
        canvas = renderer.manager.canvas
        live_renderer = canvas.get_renderer()
        pixels = np.asarray(live_renderer.buffer_rgba()).copy()
        for fmt in ("png", "svg", "pdf"):
            renderer.export(fmt, io.BytesIO())
            self.assertIs(canvas.get_renderer(), live_renderer)
            self.assertEqual(canvas.get_width_height(), (320, 240))
            np.testing.assert_array_equal(np.asarray(canvas.get_renderer().buffer_rgba()), pixels)

    def test_narrow_viewport_keeps_axis_labels_and_colorbars_inside_figure(self):
        for kind in ("time", "trajectory3d", "surface", "scatter3d"):
            with self.subTest(kind=kind):
                renderer = Renderer()
                payload = figure(kind)
                payload.update(width=10, height=6, dpi=120, fontSize=10)
                panel = payload["panels"][0]
                panel["series"][0].update(xLabel="time (s)", yLabel="Voltage (V)", zLabel="Current (A)")
                panel["surface"].update(xLabel="Column index (1-based)", yLabel="Row index (1-based)", zLabel="Temperature (K)")
                if kind == "scatter3d":
                    panel["series"][0]["color"] = [1, 2, 3, 4]
                renderer.render(payload)
                canvas = renderer.manager.canvas
                canvas.handle_resize({"width": 460, "height": 640})
                canvas.draw()
                for axis in renderer.figure.axes:
                    bounds = axis.get_tightbbox(canvas.get_renderer())
                    self.assertGreaterEqual(bounds.x0, 0)
                    self.assertGreaterEqual(bounds.y0, 0)
                    self.assertLessEqual(bounds.x1, 460)
                    self.assertLessEqual(bounds.y1, 640)
                if kind in {"surface", "scatter3d"}:
                    plot, colorbar = renderer.figure.axes
                    self.assertLess(colorbar.get_position().y1, plot.get_position().y0)

    def test_axis_linking_requires_same_quantity_and_units(self):
        renderer = Renderer()
        payload = figure()
        payload.update(columns=3)
        panel = payload["panels"][0]
        panel["linkX"] = "time"
        payload["panels"] = [copy.deepcopy(panel) for _ in range(3)]
        payload["panels"][2]["series"][0]["xUnit"] = "ms"
        renderer.render(payload)
        a, b, c = renderer.figure.axes
        self.assertTrue(a.get_shared_x_axes().joined(a, b))
        self.assertFalse(a.get_shared_x_axes().joined(a, c))
        a.set_xlim(.2, .8)
        self.assertEqual(a.get_xlim(), b.get_xlim())

    def test_invalid_versions_and_limits_rejected(self):
        renderer = Renderer()
        for changes in ({"version": 2}, {"rows": 0}, {"width": 100, "height": 100, "dpi": 1200}):
            payload = figure()
            payload.update(changes)
            with self.assertRaises(ValueError):
                renderer.render(payload)
        renderer.render(figure())
        with self.assertRaises(ValueError):
            renderer.export("py", io.BytesIO())

    def test_playback_preserves_3d_camera(self):
        renderer = Renderer()
        payload = figure("trajectory3d")
        renderer.render(payload)
        renderer.figure.axes[0].view_init(42, 17, 3)
        payload["time"] = 2
        renderer.render(payload)
        ax = renderer.figure.axes[0]
        self.assertEqual((ax.elev, ax.azim, ax.roll), (42, 17, 3))

    def test_updates_preserve_live_canvas_size_and_dpi(self):
        renderer = Renderer()
        payload = figure()
        renderer.render(payload)
        canvas = renderer.manager.canvas
        for ratio in (1, 1.5, 2):
            canvas.handle_set_device_pixel_ratio({"device_pixel_ratio": ratio})
            canvas.handle_resize({"width": 460, "height": 640})
            canvas.draw()
            expected_dpi = renderer.figure.dpi
            payload["time"] = ratio
            payload.update(width=12, height=8, dpi=300)
            renderer.render(payload)
            self.assertEqual(canvas.get_width_height(), (460, 640))
            self.assertEqual(canvas.get_width_height(physical=True), (int(460 * ratio), int(640 * ratio)))
            self.assertEqual(renderer.figure.dpi, expected_dpi)

    def test_initial_render_cannot_reset_viewport_or_display_dpi(self):
        renderer = Renderer()
        canvas = renderer.manager.canvas
        initial_dpi = renderer.figure.dpi
        canvas.handle_set_device_pixel_ratio({"device_pixel_ratio": 2})
        canvas.handle_resize({"width": 460, "height": 640})
        payload = figure()
        payload["dpi"] = 300
        renderer.render(payload)
        self.assertEqual(renderer.figure.dpi, initial_dpi * 2)
        self.assertEqual(canvas.get_width_height(), (460, 640))

    def test_camera_drag_does_not_relayout_axes(self):
        for kind in ("trajectory3d", "surface"):
            renderer = Renderer()
            renderer.render(figure(kind))
            ax = renderer.figure.axes[0]
            bounds = ax.get_position().bounds
            for elev, azim in ((35, -55), (40, -50), (45, -45)):
                ax.view_init(elev, azim)
                renderer.manager.canvas.draw()
                np.testing.assert_allclose(ax.get_position().bounds, bounds, atol=1e-12)

    def test_playback_preserves_3d_zoom_and_axes_identity(self):
        renderer = Renderer()
        payload = figure("trajectory3d")
        renderer.render(payload)
        ax = renderer.figure.axes[0]
        ax.set_xlim(.4, .8)
        ax.set_ylim(1.2, 1.8)
        ax.set_zlim(1.5, 2.5)
        payload["time"] = 2
        renderer.render(payload)
        self.assertIs(renderer.figure.axes[0], ax)
        self.assertEqual(ax.get_zlim(), (1.5, 2.5))
        self.assertEqual(list(ax.lines[1].get_data_3d()[2]), [1])

    def test_surface_frames_preserve_axes_camera_zoom_and_layout(self):
        renderer = Renderer()
        payload = figure("surface")
        renderer.render(payload)
        ax, colorbar = renderer.figure.axes
        ax.view_init(42, 17, 3)
        ax.set_zlim(1.5, 2.5)
        bounds = ax.get_position().bounds
        for offset in (100, 1000, 10000):
            payload["panels"][0]["surface"]["z"] = [[offset + n, offset + n + 1] for n in (1, 3, 5)]
            payload["time"] = offset
            renderer.render(payload)
            self.assertIs(renderer.figure.axes[0], ax)
            self.assertIs(renderer.figure.axes[1], colorbar)
            self.assertEqual((ax.elev, ax.azim, ax.roll), (42, 17, 3))
            self.assertEqual(ax.get_zlim(), (1.5, 2.5))
            np.testing.assert_allclose(ax.get_position().bounds, bounds, atol=1e-12)
            self.assertEqual(len(ax.collections), 1)

    def test_surface_autoscale_can_shrink_when_scrubbing_backwards(self):
        renderer = Renderer()
        payload = figure("surface")
        renderer.render(payload)
        expected = renderer.figure.axes[0].get_zlim()
        payload["panels"][0]["surface"]["z"] = [[101, 102], [103, 104], [105, 106]]
        renderer.render(payload)
        self.assertGreater(renderer.figure.axes[0].get_zlim()[0], 90)
        renderer.render(figure("surface"))
        np.testing.assert_allclose(renderer.figure.axes[0].get_zlim(), expected)

    def test_webagg_drag_continues_across_playback_without_axis_motion(self):
        for kind in ("trajectory3d", "surface"):
            renderer = Renderer()
            payload = figure(kind)
            renderer.render(payload)
            ax, canvas = renderer.figure.axes[0], renderer.manager.canvas
            bounds = ax.get_position().bounds
            x, y = ax.transAxes.transform((.5, .5))
            old_camera = (ax.elev, ax.azim, ax.roll)
            def mouse(kind, dx, dy, button=0, buttons=1):
                canvas.handle_event({"type": kind, "x": x + dx, "y": canvas.get_renderer().height - y - dy,
                                     "button": button, "buttons": buttons, "modifiers": []})
            mouse("button_press", 0, 0)
            mouse("motion_notify", 10, 5)
            canvas.draw()
            self.assertNotEqual((ax.elev, ax.azim, ax.roll), old_camera)
            payload["time"] = 2
            renderer.render(payload)
            self.assertIs(renderer.figure.axes[0], ax)
            self.assertIsNotNone(ax.button_pressed)
            mouse("motion_notify", 20, 10)
            canvas.draw()
            np.testing.assert_allclose(ax.get_position().bounds, bounds)
            mouse("button_release", 20, 10, buttons=0)
            limits = (ax.get_xlim(), ax.get_ylim(), ax.get_zlim())
            mouse("button_press", 0, 0, button=2, buttons=2)
            mouse("motion_notify", 0, 15, button=2, buttons=2)
            mouse("button_release", 0, 15, button=2, buttons=0)
            canvas.draw()
            zoomed = (ax.get_xlim(), ax.get_ylim(), ax.get_zlim())
            self.assertNotEqual(zoomed, limits)
            payload["time"] = 1
            renderer.render(payload)
            np.testing.assert_allclose((ax.get_xlim(), ax.get_ylim(), ax.get_zlim()), zoomed)

    def test_playback_keeps_manual_viewport_until_assignments_change(self):
        renderer = Renderer()
        payload = figure()
        payload["panels"][0]["series"][0]["yVariable"] = "a"
        renderer.render(payload)
        renderer.figure.axes[0].set_xlim(.4, .8)
        renderer.figure.axes[0].set_ylim(1.2, 1.8)
        payload["time"] = 2
        renderer.render(payload)
        self.assertEqual(renderer.figure.axes[0].get_xlim(), (.4, .8))
        self.assertEqual(renderer.figure.axes[0].get_ylim(), (1.2, 1.8))
        payload["panels"][0]["series"][0]["yVariable"] = "b"
        renderer.render(payload)
        self.assertNotEqual(renderer.figure.axes[0].get_xlim(), (.4, .8))
        self.assertNotEqual(renderer.figure.axes[0].get_ylim(), (1.2, 1.8))

    def test_cosmetic_labels_do_not_reset_camera_or_manual_limits(self):
        for kind in ("trajectory3d", "surface"):
            renderer = Renderer()
            payload = figure(kind)
            renderer.render(payload)
            ax = renderer.figure.axes[0]
            ax.view_init(42, 17, 3)
            ax.set_xlim(.4, .8)
            ax.set_ylim(1.2, 1.8)
            ax.set_zlim(1.5, 2.5)
            panel = payload["panels"][0]
            panel["series"][0]["label"] = "New legend"
            panel["surface"].update(xLabel="Column", yLabel="Row", zLabel="Temperature")
            renderer.render(payload)
            ax = renderer.figure.axes[0]
            self.assertEqual((ax.elev, ax.azim, ax.roll), (42, 17, 3))
            self.assertEqual(ax.get_xlim(), (.4, .8))
            self.assertEqual(ax.get_ylim(), (1.2, 1.8))
            self.assertEqual(ax.get_zlim(), (1.5, 2.5))

    def test_automatic_variable_legend_excludes_playback_markers(self):
        renderer = Renderer()
        payload = figure()
        payload["panels"][0]["series"][0].update(label="", yVariable="voltage")
        renderer.render(payload)
        self.assertEqual([text.get_text() for text in renderer.figure.axes[0].get_legend().get_texts()], ["voltage"])

    def test_converted_playback_cursor_and_resolved_axis_labels(self):
        renderer = Renderer()
        payload = figure()
        payload["panels"][0]["series"][0].update(x=[0, 1000, 1000, 2000], xScale=1000, xUnit="ms")
        renderer.render(payload)
        self.assertEqual(list(renderer.figure.axes[0].lines[-1].get_xdata()), [1000, 1000])
        payload = figure("surface")
        payload["panels"][0]["surface"]["zLabel"] = "Temperature (K)"
        renderer.render(payload)
        self.assertEqual(renderer.figure.axes[0].get_zlabel(), "Temperature (K)")
        payload = figure("histogram")
        payload["panels"][0]["series"][0]["yLabel"] = "Voltage (V)"
        renderer.render(payload)
        self.assertEqual(renderer.figure.axes[0].get_xlabel(), "Voltage (V)")
        self.assertEqual(renderer.figure.axes[0].get_ylabel(), "Count")

    def test_display_reduction_preserves_spikes_gaps_and_full_export(self):
        count = 20000
        y = np.zeros(count)
        y[1234], y[5678], y[7890] = 500, -200, np.nan
        selected = envelope_indices(y)
        self.assertLess(len(selected), 4001)
        self.assertTrue({1234, 5678, 7890}.issubset(set(selected)))
        payload = figure()
        payload["panels"][0]["series"] = [{"x": list(range(count)), "y": y.tolist()}]
        renderer = Renderer()
        renderer.render(payload)
        self.assertTrue(renderer.reduced)
        self.assertLess(len(renderer.figure.axes[0].lines[0].get_xdata()), count)
        self.assertEqual(len(renderer.payload["panels"][0]["series"][0]["x"]), count)
        rendered_counts = []
        original_render = Renderer.render

        def inspect_complete(instance, *args, **kwargs):
            result = original_render(instance, *args, **kwargs)
            rendered_counts.append(len(instance.figure.axes[0].lines[0].get_xdata()))
            return result

        with patch.object(Renderer, "render", inspect_complete):
            renderer.export("svg", io.BytesIO())
        self.assertEqual(rendered_counts, [count])


class WorkerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.log = open(Path(self.temp.name) / "worker.log", "w+", encoding="utf-8")
        self.process = subprocess.Popen([sys.executable, str(Path(__file__).with_name("analysis_worker.py")),
                                         "--token", "unit-test-session", "--port", "0"],
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.log,
                                        text=True, encoding="utf-8")
        self.responses = queue.Queue()

        def read():
            for line in self.process.stdout:
                self.responses.put(json.loads(line))
        self.reader = threading.Thread(target=read, daemon=True)
        self.reader.start()
        self.url = self.responses.get(timeout=20)["url"]
        self.base = self.url.split("/?")[0]

    def tearDown(self):
        if self.process.poll() is None:
            self.process.stdin.close()
            try:
                self.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)
        self.process.stdout.close()
        if not self.process.stdin.closed:
            self.process.stdin.close()
        self.log.close()
        self.temp.cleanup()

    def command(self, operation, **kwargs):
        self.process.stdin.write(json.dumps({"id": 42, "op": operation, **kwargs}) + "\n")
        self.process.stdin.flush()
        result = self.responses.get(timeout=20)
        self.assertEqual(result["id"], 42)
        return result

    def test_http_auth_local_assets_and_host(self):
        with self.assertRaises(HTTPError) as caught:
            urlopen(self.base + "/", timeout=3)
        self.assertEqual(caught.exception.code, 403)
        caught.exception.close()
        with urlopen(self.url, timeout=3) as response:
            page = response.read().decode()
            cookie = response.headers["Set-Cookie"].split(";", 1)[0]
            self.assertIn("new mpl.figure", page)
            self.assertNotIn("https://", page)
        for asset in ("/mpl.js", "/_static/css/mpl.css", "/_images/home.png"):
            with urlopen(Request(self.base + asset, headers={"Cookie": cookie}), timeout=3) as response:
                self.assertGreater(len(response.read()), 0)
        with self.assertRaises(HTTPError) as caught:
            urlopen(Request(self.url, headers={"Host": "evil.example"}), timeout=3)
        self.assertEqual(caught.exception.code, 403)
        caught.exception.close()

    def test_render_export_recovery_and_shutdown(self):
        self.assertFalse(self.command("execute", code="raise Exception()" )["ok"])
        self.assertFalse(self.command("render", figure={"version": 99})["ok"])
        unresolved = figure()
        unresolved["panels"][0]["error"] = "Variable disappeared"
        response = self.command("render", figure=unresolved)
        self.assertFalse(response["ok"])
        self.assertIn("Variable disappeared", response["error"])
        self.assertTrue(self.command("render", figure=figure())["ok"])
        target = str(Path(self.temp.name) / "figure.svg")
        self.assertTrue(self.command("export", format="svg", path=target)["ok"])
        self.assertIn("<svg", Path(target).read_text())
        self.assertTrue(self.command("shutdown")["ok"])
        self.assertEqual(self.process.wait(timeout=5), 0)

    def test_websocket_auth_origin_and_real_image_events(self):
        self.assertTrue(self.command("render", figure=figure())["ok"])

        async def check():
            uri = self.base.replace("http:", "ws:") + "/ws"
            for target, headers in ((uri, {}), (uri + "?token=unit-test-session", {"Origin": "http://evil.example"})):
                with self.assertRaises(HTTPClientError):
                    await websocket_connect(HTTPRequest(target, headers=headers, connect_timeout=3))
            socket = await websocket_connect(HTTPRequest(uri + "?token=unit-test-session", headers={"Origin": self.base}, connect_timeout=3))
            try:
                await socket.write_message(json.dumps({"type": "supports_binary", "value": True}))
                await socket.write_message(json.dumps({"type": "draw"}))
                messages = []
                for _ in range(10):
                    message = await asyncio.wait_for(socket.read_message(), 5)
                    messages.append(message)
                    if isinstance(message, bytes):
                        self.assertTrue(message.startswith(b"\x89PNG"))
                        break
                else:
                    self.fail("WebAgg did not send its canvas image")
                await socket.write_message(json.dumps({"type": "toolbar_button", "name": "__class__"}))
                while True:
                    message = await asyncio.wait_for(socket.read_message(), 5)
                    if isinstance(message, str) and "Invalid figure interaction" in message:
                        break
            finally:
                socket.close()
        asyncio.run(check())

    def test_eof_shutdown(self):
        self.process.stdin.close()
        self.assertEqual(self.process.wait(timeout=5), 0)

    def test_shutdown_ack_then_eof_keeps_cleanup_event_loop_running(self):
        async def check():
            uri = self.base.replace("http:", "ws:") + "/ws?token=unit-test-session"
            socket = await websocket_connect(HTTPRequest(uri, connect_timeout=3))
            try:
                self.assertTrue(self.command("shutdown")["ok"])
                self.process.stdin.close()
                while await asyncio.wait_for(socket.read_message(), 5) is not None:
                    pass
            finally:
                socket.close()
        asyncio.run(check())
        self.assertEqual(self.process.wait(timeout=5), 0)

    def test_two_windows_keep_independent_authentication_cookies(self):
        other = subprocess.Popen([sys.executable, str(Path(__file__).with_name("analysis_worker.py")),
                                  "--token", "second-window", "--port", "0"],
                                 stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.log,
                                 text=True, encoding="utf-8")
        ready = queue.Queue()
        reader = threading.Thread(target=lambda: ready.put(json.loads(other.stdout.readline())), daemon=True)
        reader.start()
        try:
            second_url = ready.get(timeout=20)["url"]
            jar = CookieJar()
            browser = build_opener(HTTPCookieProcessor(jar))
            for url in (self.url, second_url):
                with browser.open(url, timeout=3) as response:
                    response.read()
            self.assertEqual(len(list(jar)), 2)
            for url in (self.url, second_url):
                with browser.open(url.split("/?")[0] + "/mpl.js", timeout=3) as response:
                    self.assertIn(b"mpl.figure", response.read())
        finally:
            other.stdin.close()
            try:
                other.wait(timeout=5)
            except subprocess.TimeoutExpired:
                other.kill()
                other.wait(timeout=5)
            other.stdout.close()


if __name__ == "__main__":
    unittest.main()
