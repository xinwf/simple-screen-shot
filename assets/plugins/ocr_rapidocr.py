#!/usr/bin/env python3
"""SimpleScreenShot OCR plugin: RapidOCR first, tesseract as fallback.

Protocol (see src/media/ocr_runner.cpp):
    ocr_rapidocr.py --image-path=<png> [--del-image=true]

Recognized text is written to stdout (one block per line); the caller reads
stdout as the OCR result. Diagnostics go to stderr only, so stdout stays clean.
"""
import importlib
import os
import shutil
import subprocess
import sys


def parse_args(argv):
    image = None
    del_image = False
    for arg in argv:
        if arg.startswith("--image-path="):
            image = arg.split("=", 1)[1].strip('"')
        elif arg == "--del-image=true":
            del_image = True
    return image, del_image


def load_rapidocr():
    """Load either the current or legacy RapidOCR package.

    Importing native dependencies can fail with OSError (missing shared library)
    or ValueError (incompatible NumPy), not just ImportError. Treat all of
    those as an unavailable engine so tesseract still gets a chance.
    """
    errors = []
    for name in ("rapidocr", "rapidocr_onnxruntime"):
        try:
            return importlib.import_module(name).RapidOCR
        except Exception as exc:
            errors.append(f"{name}: {type(exc).__name__}: {exc}")
    print("[ocr plugin] RapidOCR unavailable; falling back to tesseract: "
          + "; ".join(errors), file=sys.stderr)
    return None


def extract_rapidocr_text(out):
    """Return text, '' for no detections, or None for an unknown result format."""
    # rapidocr >= 3 returns a RapidOCROutput object with .txts;
    # older versions return (result, elapse) with result = [[box, text, score], ...].
    if hasattr(out, "txts"):
        txts = out.txts
    elif isinstance(out, tuple) and out and isinstance(out[0], (list, tuple)):
        txts = []
        for item in out[0]:
            if isinstance(item, (list, tuple)) and len(item) >= 2:
                txts.append(item[1])
            elif isinstance(item, dict):
                txts.append(item.get("text") or item.get("txt") or "")
    else:
        return None

    if txts is None:
        return ""
    if isinstance(txts, str):
        return txts.strip()
    # Do not use `if not txts`: some versions expose a NumPy array, whose truth
    # value is ambiguous and would otherwise abort the fallback path.
    lines = []
    for text in txts:
        if text is None:
            continue
        line = str(text).strip()
        if line:
            lines.append(line)
    return "\n".join(lines)


def recognize_rapidocr(image):
    """Returns text ('' when nothing found), or None when RapidOCR is unusable."""
    engine = load_rapidocr()
    if engine is None:
        return None
    try:
        text = extract_rapidocr_text(engine()(image))
    except Exception as exc:
        print(f"[ocr plugin] rapidocr failed: {exc}", file=sys.stderr)
        return None
    if text is None:
        print("[ocr plugin] rapidocr returned an unsupported result; "
              "falling back to tesseract", file=sys.stderr)
    return text


def recognize_tesseract(image):
    tesseract = shutil.which("tesseract")
    if not tesseract:
        print("[ocr plugin] tesseract not found either; install rapidocr "
              "(pip install rapidocr) or tesseract-ocr + tesseract-ocr-chi-sim",
              file=sys.stderr)
        return None

    # Minimal and non-Chinese installations often only ship eng. Selecting a
    # missing chi_sim+eng makes the entire fallback fail even though English OCR
    # is available. Keep the combined language when both packs are installed.
    languages = ["chi_sim+eng"]
    try:
        listed = subprocess.run([tesseract, "--list-langs"], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, encoding="utf-8",
                                errors="replace", timeout=15)
        if listed.returncode == 0:
            installed = {line.strip() for line in listed.stdout.splitlines()
                         if line.strip() and not line.lower().startswith("list of")}
            if "chi_sim" in installed and "eng" in installed:
                languages = ["chi_sim+eng"]
            elif "chi_sim" in installed:
                languages = ["chi_sim"]
            elif "eng" in installed:
                languages = ["eng"]
    except Exception as exc:
        print(f"[ocr plugin] tesseract language probe failed: {exc}", file=sys.stderr)

    try:
        proc = subprocess.run([tesseract, image, "stdout", "-l", languages[0]],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              encoding="utf-8", errors="replace", timeout=120)
    except Exception as exc:
        print(f"[ocr plugin] tesseract failed: {exc}", file=sys.stderr)
        return None
    if proc.returncode != 0:
        detail = proc.stderr.strip()[:500]
        print(f"[ocr plugin] tesseract exited with {proc.returncode}: {detail}",
              file=sys.stderr)
        return None
    return proc.stdout.strip() or ""


def check_support():
    """Exit 0 when RapidOCR is usable, 1 otherwise. Fast: imports only, no engine init."""
    if load_rapidocr() is None:
        return 1
    try:
        importlib.import_module("onnxruntime")  # the plugin's default inference engine
        return 0
    except Exception as exc:
        print(f"[ocr plugin] onnxruntime unsupported: {type(exc).__name__}: {exc}",
              file=sys.stderr)
        return 1


def main():
    # OCR text is UTF-8 regardless of the desktop locale (including an ASCII
    # locale on older Linux distributions). Keep diagnostics off stdout.
    for stream in (sys.stdout, sys.stderr):
        reconfigure = getattr(stream, "reconfigure", None)
        if callable(reconfigure):
            try:
                reconfigure(encoding="utf-8", errors="backslashreplace")
            except (OSError, ValueError):
                pass

    if "--check" in sys.argv[1:]:
        return check_support()

    image, del_image = parse_args(sys.argv[1:])
    if not image or not os.path.isfile(image):
        print("[ocr plugin] missing --image-path", file=sys.stderr)
        return 1

    try:
        text = recognize_rapidocr(image)
        if text is None:
            text = recognize_tesseract(image)
    finally:
        if del_image:
            try:
                os.remove(image)
            except OSError:
                pass

    if text is None:
        return 1
    if text:
        print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
