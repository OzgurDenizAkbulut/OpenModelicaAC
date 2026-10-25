"""Standalone validator regressions: python tools/development/test-stage-analysis-runtime.py."""

import importlib.util
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import venv

spec = importlib.util.spec_from_file_location("stage_analysis_runtime", Path(__file__).with_name("stage-analysis-runtime.py"))
stage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stage)


class RuntimeValidationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve() / "runtime"
        self.root.mkdir()

    def interpreter(self):
        python = stage.executable(self.root)
        python.parent.mkdir(parents=True, exist_ok=True)
        python.write_bytes(b"test interpreter")
        return python

    def symlink(self, link, target, directory=False):
        try:
            link.symlink_to(target, target_is_directory=directory)
        except (OSError, NotImplementedError) as error:
            self.skipTest(f"Symlinks unavailable: {error}")

    def test_manifest_and_intended_root_forwarded_without_modification(self):
        python = self.interpreter()
        (self.root / "payload").write_text("package", encoding="utf-8")
        manifest = self.root / "analysis-runtime-manifest.json"
        manifest.write_text(json.dumps({"version": 1, "files": stage.hashes(self.root)}), encoding="utf-8")
        original = manifest.read_bytes()
        with patch.object(stage, "execute", return_value='{"isolated":1}') as execute, patch("sys.stdout", new_callable=io.StringIO):
            stage.validate(self.root)
        execute.assert_called_once_with(python, stage.VALIDATE, arguments=(self.root,))
        self.assertEqual(manifest.read_bytes(), original)

    def test_tampering_and_unsupported_manifest_rejected_before_execution(self):
        self.interpreter()
        manifest = self.root / "analysis-runtime-manifest.json"
        for content, message in (({"version": 99, "files": {}}, "unsupported"), ({"version": 1}, "Invalid"),
                                 ({"version": 1, "files": {}}, "differ")):
            with self.subTest(content=content):
                manifest.write_text(json.dumps(content), encoding="utf-8")
                with patch.object(stage, "execute") as execute, self.assertRaisesRegex(RuntimeError, message):
                    stage.validate(self.root)
                execute.assert_not_called()

    def test_nested_manifest_is_part_of_hashed_payload(self):
        nested = self.root / "package"
        nested.mkdir()
        (nested / "analysis-runtime-manifest.json").write_text("payload", encoding="utf-8")
        self.assertIn("package/analysis-runtime-manifest.json", stage.hashes(self.root))

    def test_external_interpreter_symlink_rejected_before_execution(self):
        python = stage.executable(self.root)
        python.parent.mkdir(parents=True, exist_ok=True)
        self.symlink(python, Path(sys.executable).resolve())
        with patch.object(stage, "execute") as execute, self.assertRaisesRegex(RuntimeError, "Interpreter escapes"):
            stage.validate(self.root)
        execute.assert_not_called()

    def test_external_file_and_directory_symlinks_rejected(self):
        self.interpreter()
        outside = Path(self.temp.name) / "external"
        outside.mkdir()
        (outside / "package.py").write_text("payload", encoding="utf-8")
        for target in (outside, outside / "package.py"):
            link = self.root / "escape"
            self.symlink(link, target, target.is_dir())
            with self.assertRaisesRegex(RuntimeError, "Runtime path escapes"):
                stage.validate(self.root)
            link.unlink()

    def test_internal_interpreter_alias_remains_valid(self):
        python = self.interpreter()
        alias = self.root / "internal-alias"
        self.symlink(alias, python)
        self.assertIn("internal-alias", stage.hashes(self.root))

    def test_subprocess_checks_supplied_root_not_interpreter_derived_root(self):
        with self.assertRaisesRegex(RuntimeError, "Interpreter escapes the requested runtime"):
            stage.execute(sys.executable, stage.VALIDATE, arguments=(self.root,))

    def test_copied_virtual_environment_still_rejects_external_base(self):
        venv.EnvBuilder(with_pip=False, symlinks=False).create(self.root)
        python = self.root / ("Scripts/python.exe" if os.name == "nt" else "bin/python3")
        with self.assertRaisesRegex(RuntimeError, "base_prefix escapes the requested runtime"):
            stage.execute(python, stage.VALIDATE, arguments=(self.root,))


if __name__ == "__main__":
    unittest.main()
