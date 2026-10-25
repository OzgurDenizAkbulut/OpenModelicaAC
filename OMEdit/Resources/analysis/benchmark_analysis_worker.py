"""Measure actual render and export costs; no performance assertions.

Run: python OMEdit/Resources/analysis/benchmark_analysis_worker.py
"""

import io
import json
import platform
from pathlib import Path
import sys
import time

import matplotlib
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from analysis_worker import Renderer


def measure(name, payload):
    renderer = Renderer()
    start = time.perf_counter()
    errors = renderer.render(payload)
    rendered = time.perf_counter()
    image = io.BytesIO()
    renderer.export("png", image)
    exported = time.perf_counter()
    axis = renderer.figure.axes[0]
    axis.set_xlim(*axis.get_xlim())
    renderer.manager.canvas.draw()
    navigated = time.perf_counter()
    return {"case": name, "renderSeconds": rendered - start, "exportPngSeconds": exported - rendered,
            "redrawSeconds": navigated - exported, "pngBytes": len(image.getvalue()), "errors": errors}


def main():
    x = np.linspace(0, 100, 100_000)
    curves = [{"x": x.tolist(), "y": np.sin(x + i).tolist(), "time": x.tolist(), "label": str(i)} for i in range(10)]
    coords = np.linspace(-3, 3, 100)
    xx, yy = np.meshgrid(coords, coords)
    results = [measure("10 curves x 100000 samples", {"version": 1, "panels": [{"type": "time", "series": curves}]}),
               measure("100 x 100 surface", {"version": 1, "panels": [{"type": "surface", "surface": {
                   "x": coords.tolist(), "y": coords.tolist(), "z": np.sin(xx * yy).tolist()}}]})]
    print(json.dumps({"python": platform.python_version(), "platform": platform.platform(),
                      "matplotlib": matplotlib.__version__, "measurements": results}, indent=2))


if __name__ == "__main__":
    main()
