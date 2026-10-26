#!/usr/bin/env python3
"""Package and validate a native arm64 installation; requires macOS and Qt tools."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import tempfile


def run(*args, **kwargs):
    print('+', ' '.join(map(str, args)), flush=True)
    return subprocess.run(list(map(str, args)), check=True, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          timeout=kwargs.pop('timeout', 300), **kwargs).stdout


def macho(path):
    if not path.is_file() or path.is_symlink():
        return False
    with path.open('rb') as stream:
        return stream.read(4) in (b'\xcf\xfa\xed\xfe', b'\xfe\xed\xfa\xcf',
                                  b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca')


def dependencies(path):
    return [line.strip().split(' (compatibility version', 1)[0]
            for line in run('otool', '-L', path).splitlines()[1:]]


def relocate(app):
    frameworks = app / 'Contents/Frameworks'
    frameworks.mkdir(exist_ok=True)
    copied = {}
    visited = set()
    compiler = next((app / 'Contents/Resources/lib').rglob('libOpenModelicaCompiler.dylib'))
    while True:
        pending = [p for p in app.rglob('*') if p not in visited and macho(p)]
        if not pending:
            break
        for binary in pending:
            visited.add(binary)
            for dep in dependencies(binary):
                if dep.endswith('/libOpenModelicaCompiler.dylib'):
                    # The C compiler infers its resource prefix from its own lib/ path.
                    replacement = '@loader_path/' + os.path.relpath(compiler, binary.parent)
                    run('install_name_tool', '-change', dep, replacement, binary)
                    continue
                if not dep.startswith('/') or dep.startswith(('/System/Library/', '/usr/lib/')):
                    continue
                source = Path(dep)
                if source.is_relative_to(app):
                    target = source
                else:
                    if '.framework/' in dep:
                        raise RuntimeError(f'Qt deployment left an external framework: {dep}')
                    target = frameworks / source.name
                    if target in copied and copied[target] != source.resolve():
                        raise RuntimeError(f'Conflicting dependency names: {dep}')
                    if not target.exists():
                        shutil.copy2(source.resolve(strict=True), target)
                    copied[target] = source.resolve()
                replacement = '@loader_path/' + os.path.relpath(target, binary.parent)
                run('install_name_tool', '-change', dep, replacement, binary)
            if binary.suffix in ('.dylib', '.so'):
                run('install_name_tool', '-id', '@loader_path/' + binary.name, binary)
    return visited


def audit(app, binaries):
    executable_dir = app / 'Contents/MacOS'
    def expand(value, binary):
        return Path(value.replace('@loader_path', str(binary.parent))
                    .replace('@executable_path', str(executable_dir)))
    for binary in sorted(binaries):
        if 'arm64' not in run('lipo', '-archs', binary):
            raise RuntimeError(f'Missing arm64 slice: {binary}')
        lines = run('otool', '-l', binary).splitlines()
        rpaths = []
        for i, line in enumerate(lines):
            if line.strip() == 'cmd LC_RPATH':
                value = lines[i + 2].strip().removeprefix('path ').rsplit(' (offset', 1)[0]
                rpaths.append(expand(value, binary))
        for dep in dependencies(binary):
            if dep.startswith(('/System/Library/', '/usr/lib/')):
                continue
            if dep.startswith('@rpath/'):
                candidates = [p / dep[7:] for p in rpaths]
                # dyld inherits the main executable's framework search path.
                candidates.append(app / 'Contents/Frameworks' / dep[7:])
            else:
                candidates = [expand(dep, binary)]
            if not any(p.exists() and p.resolve().is_relative_to(app) for p in candidates):
                raise RuntimeError(f'Unresolved bundled dependency: {binary}: {dep}')


def validate(app, repo, evidence):
    resources = app / 'Contents/Resources'
    env = os.environ.copy()
    for key in ('DYLD_LIBRARY_PATH', 'DYLD_FRAMEWORK_PATH', 'QT_PLUGIN_PATH', 'PYTHONPATH', 'PYTHONHOME'):
        env.pop(key, None)
    env.update(OPENMODELICAHOME=str(resources), PATH='/usr/bin:/bin:/usr/sbin:/sbin',
               SDKROOT=run('xcrun', '--sdk', 'macosx', '--show-sdk-path').strip())
    omc = app / 'Contents/MacOS/omc'
    version = run(omc, '--version', env=env)
    (evidence / 'compiler-version.txt').write_text(version)
    with tempfile.TemporaryDirectory(prefix='FST simulation ') as temp:
        script = Path(temp) / 'smoke.mos'
        script.write_text('loadString("model FSTSmoke Real x(start=1, fixed=true); equation der(x)=-x; end FSTSmoke;"); getErrorString();\n'
                          'setCompiler("clang"); setCXXCompiler("clang++");\n'
                          'simulate(FSTSmoke, stopTime=1); getErrorString();\n')
        output = run(omc, script, cwd=temp, env=env, timeout=180)
        (evidence / 'simulation.txt').write_text(output)
        if not (Path(temp) / 'FSTSmoke_res.mat').is_file():
            raise RuntimeError('Packaged compiler did not produce a simulation result: ' + output)
    output = run(sys.executable, repo / 'tools/development/stage-analysis-runtime.py',
                 '--validate-runtime', resources / 'share/omedit/analysis/runtime', env=env)
    (evidence / 'analysis-runtime.txt').write_text(output)
    with tempfile.TemporaryDirectory(prefix='FST settings ') as temp:
        env.update(HOME=temp, QT_QPA_PLATFORM='offscreen')
        output = run(app / 'Contents/MacOS/OpenModelica_FST', '--help', env=env, timeout=60)
        (evidence / 'gui-help.txt').write_text(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--qt', type=Path, required=True)
    args = parser.parse_args()
    if sys.platform != 'darwin':
        parser.error('Packaging must run on macOS')
    repo = Path(__file__).resolve().parents[2]
    install, output = args.install.resolve(strict=True), args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    stage = output / 'image'
    stage.mkdir()
    app = stage / 'OpenModelica_FST.app'
    shutil.copytree(install / 'Applications/OMEdit.app', app, symlinks=True)
    contents = app / 'Contents'
    resources = contents / 'Resources'
    resources.mkdir(exist_ok=True)
    for name in ('lib', 'share', 'include'):
        shutil.copytree(install / name, resources / name, symlinks=True, dirs_exist_ok=True)
    shutil.copytree(install / 'bin', contents / 'MacOS', symlinks=True, dirs_exist_ok=True)
    (resources / 'bin').symlink_to('../MacOS', target_is_directory=True)
    (contents / 'share').symlink_to('Resources/share', target_is_directory=True)
    with (contents / 'Info.plist').open('wb') as stream:
        plistlib.dump(dict(CFBundleExecutable='OMEdit', CFBundleName='OpenModelica_FST',
                          CFBundleDisplayName='OpenModelica_FST', CFBundlePackageType='APPL',
                          CFBundleIdentifier='org.openmodelica.fst.development',
                          CFBundleShortVersionString='1.28.0', CFBundleVersion='1.28.0',
                          CFBundleIconFile='omedit.icns', LSMinimumSystemVersion='15.0',
                          NSHighResolutionCapable=True), stream)
    run(args.qt / 'bin/macdeployqt', app, '-always-overwrite', '-no-codesign',
        '-executable=' + str(contents / 'MacOS/omc'), timeout=600)
    binaries = relocate(app)
    audit(app, binaries)
    launcher = contents / 'MacOS/OpenModelica_FST'
    launcher.write_text('#!/bin/bash\nset -e\n'
                        'here="$(cd "$(dirname "$0")" && pwd)"\n'
                        'export OPENMODELICAHOME="$here/../Resources"\n'
                        'export PATH="$here:/usr/bin:/bin:/usr/sbin:/sbin:$PATH"\n'
                        'export SDKROOT="$(/usr/bin/xcrun --sdk macosx --show-sdk-path)"\n'
                        'exec "$here/OMEdit" "$@"\n')
    launcher.chmod(0o755)
    plist = plistlib.loads((contents / 'Info.plist').read_bytes())
    plist['CFBundleExecutable'] = launcher.name
    (contents / 'Info.plist').write_bytes(plistlib.dumps(plist))
    (resources / 'share/omedit/components.ini').write_text('[Components]\nLocalAutocomplete=true\nAnalysisFigures=true\n')
    licenses = resources / 'licenses'
    licenses.mkdir(exist_ok=True)
    for path in repo.iterdir():
        if path.is_file() and any(word in path.name.upper() for word in ('LICENSE', 'COPYING', 'NOTICE')):
            shutil.copy2(path, licenses / path.name)
    for prefix in sorted(Path('/opt/homebrew/Cellar').glob('*/*')):
        for path in prefix.glob('*'):
            if path.is_file() and any(word in path.name.upper() for word in ('LICENSE', 'COPYING', 'NOTICE')):
                dest = licenses / prefix.parent.name / path.name
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, dest)
    run('codesign', '--force', '--deep', '--sign', '-', app, timeout=600)
    run('codesign', '--verify', '--deep', '--strict', app)
    evidence = output.parent / 'logs'
    # Validate after moving to a path with spaces, with the install tree hidden.
    moved = stage / 'OpenModelica FST.app'
    app.rename(moved)
    hidden = install.with_name(install.name + '-validation-hidden')
    install.rename(hidden)
    try:
        validate(moved, repo, evidence)
    finally:
        hidden.rename(install)
        moved.rename(app)
    revision = run('git', 'rev-parse', 'HEAD', cwd=repo).strip()
    manifest = {'source_revision': revision, 'architecture': 'arm64', 'minimum_macos': '15.0',
                'signing': 'ad-hoc; not notarized', 'checks': ['dependency audit', 'arm64',
                'codesign', 'relocated simulation', 'analysis rendering', 'GUI help'],
                'files': {p.relative_to(app).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                          for p in sorted(app.rglob('*')) if p.is_file() and not p.is_symlink()}}
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (stage / 'Applications').symlink_to('/Applications', target_is_directory=True)
    (stage / 'README.txt').write_text('OpenModelica_FST development build for Apple Silicon, macOS 15+.\n'
        'Drag the app to Applications. Ad-hoc signed, not Apple notarized.\n'
        'Simulation requires Apple Xcode Command Line Tools (xcode-select --install).\n'
        'Local autocomplete requires a separately configured compatible server and model.\n'
        'Analysis Python is included. Fortran, optimization, 3D animation and OMSimulator are disabled.\n')
    dmg = output / f'OpenModelica_FST-1.28-dev-{revision[:10]}-macos-arm64.dmg'
    run('hdiutil', 'create', '-volname', 'OpenModelica_FST', '-srcfolder', stage,
        '-format', 'UDZO', dmg, timeout=600)
    run('hdiutil', 'verify', dmg)
    digest = hashlib.sha256(dmg.read_bytes()).hexdigest()
    dmg.with_suffix('.dmg.sha256').write_text(f'{digest}  {dmg.name}\n')


if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError as error:
        print(error.stdout, file=sys.stderr)
        raise
