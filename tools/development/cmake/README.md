# Development Qt WebEngine adapters

The workspace UCRT64 Qt SDK contains WebEngine DLLs, import libraries and headers
without WebEngine CMake package files. These adapters expose the existing SDK
libraries to the normal top-level build. They do not replace compiler libraries
or generated APIs, and do not modify the SDK.

Use them only with that incomplete SDK:

```text
-DOM_DEVELOPMENT_QT_ROOT=<sdk>/ucrt64
-DQt6WebEngineCore_DIR=<checkout>/tools/development/cmake/Qt6WebEngineCore
-DQt6WebEngineWidgets_DIR=<checkout>/tools/development/cmake/Qt6WebEngineWidgets
```

The imported dependencies still use the SDK's normal Qt CMake packages. Versions
come from the WebEngine headers; the current SDK has WebEngine 6.11.0 alongside
Qt 6.11.1. This combination requires the candidate GUI runtime checks before
adoption. A complete SDK should use its official package files instead.

The incomplete SDK also needs its WebEngine helper, resources and locales. Copy
these from the independent workspace toolchain before configuring the GUI:

```text
python tools/development/stage-webengine-sdk.py --sdk <sdk>/ucrt64 --toolchain-prefix <workspace-toolchain>/ucrt64
```

The script verifies hashes, refuses to replace differing existing assets and writes
`webengine-runtime-provenance.json` into the SDK. It does not write into a candidate
installation. The current source provides `share/qt6/bin/QtWebEngineProcess.exe`,
six files under `share/qt6/resources` and 53 locale packs under
`share/qt6/translations/qtwebengine_locales`.

Set `-DOM_QT_DEPLOYMENT_PREFIX=<sdk>/ucrt64` so OMEdit and OMPlot both use the SDK's
deployment tool, DLLs, QML modules and plugins. The independent MSYS toolchain
remains a fallback for non-Qt runtime dependencies. Keep this consistent with
`CMAKE_PREFIX_PATH` and `OM_DEVELOPMENT_QT_ROOT`.
