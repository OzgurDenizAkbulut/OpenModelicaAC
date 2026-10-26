#!/bin/bash
set -euo pipefail
test "$(uname -s)" = Darwin
test "$(uname -m)" = arm64
cd "$(dirname "$0")/../.."
repo=$PWD
build=$repo/build_cmake/fst-macos
mkdir -p "$build/logs"
exec > >(tee "$build/logs/build.log") 2>&1

git rev-parse HEAD > "$build/logs/source-revision.txt"
git config submodule.OMCompiler/3rdParty.url https://github.com/OpenModelica/OMCompiler-3rdParty.git
git config submodule.OMCompiler/Compiler/boot/bomc.url https://github.com/OpenModelica/OMBootstrapping.git
git config submodule.OMEdit/OMEditLIB/Resources/crml-diagram.url https://github.com/OpenModelica/CRML-diagram.git
git submodule update --init --depth 1 OMCompiler/3rdParty OMCompiler/Compiler/boot/bomc OMEdit/OMEditLIB/Resources/crml-diagram
git -C OMCompiler/3rdParty submodule update --init --depth 1 SuiteSparse sundials fmi-library
git submodule status --recursive > "$build/logs/submodules.txt"

brew install cmake ninja ccache pkgconf openjdk libomp gettext libiconv qt qtwebengine qt5compat qtsvg uv
brew list --versions > "$build/logs/brew-versions.txt"
brew info --json=v2 --installed > "$build/logs/brew-provenance.json"
export PATH="$(brew --prefix openjdk)/bin:$(brew --prefix qt)/bin:$PATH"
export SDKROOT="$(xcrun --sdk macosx --show-sdk-path)"
export CCACHE_DIR="$build/ccache"
export UV_PYTHON_INSTALL_DIR="$build/python-download"
uv python install 3.12
py=$(uv python find --managed-python 3.12)
runtime=$build/analysis-runtime
python3 - "$py" "$runtime" <<'PY'
import pathlib, shutil, sys
source = pathlib.Path(sys.argv[1]).resolve().parent.parent
shutil.copytree(source, sys.argv[2], symlinks=True)
PY
uv pip install --break-system-packages --python "$runtime/bin/python3" -r OMEdit/Resources/analysis/requirements.txt packaging
uv pip freeze --python "$runtime/bin/python3" > "$build/logs/analysis-requirements.lock"
python3 tools/development/stage-analysis-runtime.py --validate-runtime "$runtime"
cmake -P tools/development/test-macos-architecture.cmake

cmake -S . -B "$build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$build/install_cmake" \
  -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DCMAKE_PREFIX_PATH="$(brew --prefix);$(brew --prefix qt);$(brew --prefix libiconv);$(brew --prefix gettext)" \
  -DOM_ENABLE_GUI_CLIENTS=ON -DOM_ENABLE_OMSIMULATOR=OFF \
  -DOM_ENABLE_OMSHELL=OFF -DOM_ENABLE_OMNOTEBOOK=OFF -DOM_ENABLE_OMSENS_QT=OFF \
  -DOM_ENABLE_TESTSUITE=OFF -DOM_OMC_ENABLE_RUST=OFF \
  -DOM_OMC_ENABLE_FORTRAN=OFF -DOM_OMC_ENABLE_OPTIMIZATION=OFF -DOM_OMC_ENABLE_MOO=OFF \
  -DOM_OMC_ENABLE_COLPACK=OFF -DOM_OMC_ENABLE_CPP_RUNTIME=OFF \
  -DOM_OMC_ENABLE_PARMODELICA=OFF -DOM_OMEDIT_DEVELOPMENT_PROFILE=ON \
  -DOM_OMEDIT_ENABLE_ANIMATION=OFF -DOM_OMEDIT_ENABLE_TESTS=ON \
  -DOM_OMEDIT_DEVELOPMENT_TESTS_ONLY=ON -DOM_OMEDIT_ANALYSIS_RUNTIME_DIR="$runtime"
cmake --build "$build" --target install --parallel 2
cmake --build "$build" --target LocalCompletionClientTest InlineCompletionTest --parallel 2
ctest --test-dir "$build/OMEdit/Testsuite" -R '^(LocalCompletionClientTest|InlineCompletionTest)$' --output-on-failure --no-tests=error
python3 tools/development/package-macos-distribution.py --install "$build/install_cmake" --output "$build/package" --qt "$(brew --prefix qt)"
mkdir -p "$build/distribution"
cp "$build/package/"*.dmg "$build/package/"*.sha256 "$build/package/manifest.json" "$build/distribution/"
