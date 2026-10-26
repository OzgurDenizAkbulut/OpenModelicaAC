"""Host-independent regressions for Mach-O dependency parsing and containment."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    'macos_package', Path(__file__).with_name('package-macos-distribution.py'))
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class DependencyTests(unittest.TestCase):
    def test_framework_install_id_is_not_a_dependency(self):
        binary = Path('/app/QtQmlModels.framework/Versions/A/QtQmlModels')
        identity = '/opt/homebrew/opt/qtdeclarative/lib/QtQmlModels.framework/Versions/A/QtQmlModels'
        dependency = '@rpath/QtCore.framework/Versions/A/QtCore'

        def output(tool, option, path):
            if option == '-D':
                return f'{path}:\n{identity}\n'
            return (f'{path}:\n\t{identity} (compatibility version 6.0.0, current version 6.11.2)\n'
                    f'\t{dependency} (compatibility version 6.0.0, current version 6.11.2)\n')

        with patch.object(package, 'run', side_effect=output):
            self.assertEqual(package.dependencies(binary), [dependency])

    def test_executable_keeps_first_dependency(self):
        binary = Path('/app/OMEdit')
        with patch.object(package, 'run', side_effect=[f'{binary}:\n',
                f'{binary}:\n\t/usr/lib/libSystem.B.dylib (compatibility version 1.0.0)\n']):
            self.assertEqual(package.dependencies(binary), ['/usr/lib/libSystem.B.dylib'])

    def test_loader_dependency_must_remain_inside_app(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            app = root / 'App With Spaces.app'
            binary = app / 'Contents/MacOS/OMEdit'
            binary.parent.mkdir(parents=True)
            binary.touch()
            outside = root / 'outside.dylib'
            outside.touch()
            inside = app / 'Contents/Frameworks/inside.dylib'
            inside.parent.mkdir()
            inside.touch()
            with patch.object(package, 'run', side_effect=lambda *args: 'arm64\n' if args[0] == 'lipo' else ''), \
                    patch.object(package, 'dependencies', return_value=['@loader_path/../../../outside.dylib']):
                with self.assertRaisesRegex(RuntimeError, 'Unresolved bundled dependency'):
                    package.audit(app, {binary})
            with patch.object(package, 'run', side_effect=lambda *args: 'arm64\n' if args[0] == 'lipo' else ''), \
                    patch.object(package, 'dependencies', return_value=['@loader_path/../Frameworks/inside.dylib']):
                package.audit(app, {binary})


if __name__ == '__main__':
    unittest.main()
