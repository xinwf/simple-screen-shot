#!/usr/bin/env python3
"""Dependency-free regression tests for the bundled OCR plugin."""
import importlib.util
import io
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest import mock


# Keep test runs from dropping __pycache__ into the shipped assets directory.
sys.dont_write_bytecode = True

PLUGIN_PATH = Path(__file__).resolve().parents[1] / "assets" / "plugins" / "ocr_rapidocr.py"
SPEC = importlib.util.spec_from_file_location("ocr_rapidocr", PLUGIN_PATH)
plugin = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(plugin)


class RapidOcrTests(unittest.TestCase):
    def test_current_output_object(self):
        out = SimpleNamespace(txts=(" 中文识别 ", "English text", None, ""))
        self.assertEqual(plugin.extract_rapidocr_text(out), "中文识别\nEnglish text")

    def test_legacy_tuple_output(self):
        out = ([[[0, 0], "first"], {"text": "second"}, [[1, 1], "third", 0.9]], [0.1])
        self.assertEqual(plugin.extract_rapidocr_text(out), "first\nsecond\nthird")

    def test_empty_outputs(self):
        self.assertEqual(plugin.extract_rapidocr_text(SimpleNamespace(txts=None)), "")
        self.assertEqual(plugin.extract_rapidocr_text(([], [0.1])), "")
        self.assertEqual(plugin.extract_rapidocr_text(SimpleNamespace(txts=())), "")

    def test_unknown_output_requests_fallback(self):
        self.assertIsNone(plugin.extract_rapidocr_text(object()))

    def test_array_like_texts_do_not_use_ambiguous_truth_value(self):
        class ArrayTexts:
            def __bool__(self):
                raise ValueError("The truth value of an array is ambiguous")

            def __iter__(self):
                return iter(("one", "two"))

        self.assertEqual(plugin.extract_rapidocr_text(SimpleNamespace(txts=ArrayTexts())),
                         "one\ntwo")

    def test_native_import_error_falls_back_to_legacy_package(self):
        legacy = SimpleNamespace(RapidOCR=object)
        with mock.patch.object(plugin.importlib, "import_module",
                               side_effect=[OSError("libgomp.so.1 missing"), legacy]) as imported:
            self.assertIs(plugin.load_rapidocr(), legacy.RapidOCR)
        self.assertEqual(imported.call_args_list,
                         [mock.call("rapidocr"), mock.call("rapidocr_onnxruntime")])

    def test_engine_failure_returns_none(self):
        engine = mock.Mock(side_effect=RuntimeError("model cannot initialize"))
        with mock.patch.object(plugin, "load_rapidocr", return_value=engine):
            self.assertIsNone(plugin.recognize_rapidocr("image.png"))

    def test_check_accepts_legacy_package(self):
        legacy = SimpleNamespace(RapidOCR=object)
        modules = {"rapidocr_onnxruntime": legacy, "onnxruntime": object()}
        with mock.patch.object(plugin.importlib, "import_module",
                               side_effect=lambda name: modules[name]):
            self.assertEqual(plugin.check_support(), 0)

    def test_check_rejects_broken_native_dependency(self):
        with mock.patch.object(plugin, "load_rapidocr", return_value=object), \
                mock.patch.object(plugin.importlib, "import_module",
                                  side_effect=ValueError("NumPy ABI mismatch")):
            self.assertEqual(plugin.check_support(), 1)


class TesseractTests(unittest.TestCase):
    @staticmethod
    def completed(stdout="", returncode=0, stderr=""):
        return SimpleNamespace(stdout=stdout, stderr=stderr, returncode=returncode)

    def run_with_languages(self, installed, result=None):
        result = result or self.completed("recognized text\n")
        calls = []

        def fake_run(args, **kwargs):
            calls.append(args)
            if args[1:] == ["--list-langs"]:
                return self.completed("List of available languages (2):\n" +
                                      "\n".join(installed) + "\n")
            return result

        with mock.patch.object(plugin.shutil, "which", return_value="/usr/bin/tesseract"), \
                mock.patch.object(plugin.subprocess, "run", side_effect=fake_run):
            text = plugin.recognize_tesseract("image.png")
        return text, calls

    def test_chinese_and_english_packs(self):
        text, calls = self.run_with_languages(["chi_sim", "eng", "osd"])
        self.assertEqual(text, "recognized text")
        self.assertEqual(calls[-1][-1], "chi_sim+eng")

    def test_english_only_install(self):
        text, calls = self.run_with_languages(["eng", "osd"])
        self.assertEqual(text, "recognized text")
        self.assertEqual(calls[-1][-1], "eng")

    def test_chinese_only_install(self):
        text, calls = self.run_with_languages(["chi_sim"])
        self.assertEqual(text, "recognized text")
        self.assertEqual(calls[-1][-1], "chi_sim")

    def test_nonzero_exit_is_failure_not_empty_success(self):
        result = self.completed("", returncode=1, stderr="language data missing")
        text, _ = self.run_with_languages(["eng"], result)
        self.assertIsNone(text)

    def test_timeout_is_failure(self):
        with mock.patch.object(plugin.shutil, "which", return_value="/usr/bin/tesseract"), \
                mock.patch.object(plugin.subprocess, "run",
                                  side_effect=subprocess.TimeoutExpired("tesseract", 15)):
            self.assertIsNone(plugin.recognize_tesseract("image.png"))


class MainTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.image = Path(self.temp.name) / "input image.png"
        self.image.write_bytes(b"fixture")

    def tearDown(self):
        self.temp.cleanup()

    def argv(self, delete=True):
        return ["ocr_rapidocr.py", "--image-path=" + str(self.image),
                "--del-image=" + ("true" if delete else "false")]

    def test_total_failure_returns_nonzero_and_cleans_image(self):
        with mock.patch.object(sys, "argv", self.argv()), \
                mock.patch.object(plugin, "recognize_rapidocr", return_value=None), \
                mock.patch.object(plugin, "recognize_tesseract", return_value=None):
            self.assertEqual(plugin.main(), 1)
        self.assertFalse(self.image.exists())

    def test_valid_empty_result_returns_zero(self):
        with mock.patch.object(sys, "argv", self.argv(delete=False)), \
                mock.patch.object(plugin, "recognize_rapidocr", return_value=""):
            self.assertEqual(plugin.main(), 0)

    def test_utf8_output_even_with_ascii_stdout(self):
        output = io.BytesIO()
        stdout = io.TextIOWrapper(output, encoding="ascii")
        with mock.patch.object(sys, "argv", self.argv(delete=False)), \
                mock.patch.object(sys, "stdout", stdout), \
                mock.patch.object(sys, "stderr", io.StringIO()), \
                mock.patch.object(plugin, "recognize_rapidocr", return_value="中文识别"):
            self.assertEqual(plugin.main(), 0)
            stdout.flush()
        self.assertEqual(output.getvalue(), "中文识别\n".encode("utf-8"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
