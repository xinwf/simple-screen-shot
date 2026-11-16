#!/usr/bin/env python3
"""SimpleScreenShot OCR plugin: RapidOCR first, tesseract as fallback.

Protocol (see src/media/ocr_runner.cpp):
    ocr_rapidocr.py --image-path=<png> [--del-image=true]

Recognized text is written to stdout (one block per line); the caller reads
stdout as the OCR result. Diagnostics go to stderr only, so stdout stays clean.
"""
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


def recognize_rapidocr(image):
    """Returns text ('' when nothing found), or None when RapidOCR is unusable."""
    try:
        from rapidocr import RapidOCR
    except ImportError:
        try:
            from rapidocr_onnxruntime import RapidOCR  # legacy package name
        except ImportError:
            print("[ocr plugin] rapidocr not installed (pip install rapidocr), "
                  "falling back to tesseract", file=sys.stderr)
            return None
    try:
        out = RapidOCR()(image)
    except Exception as exc:
        print(f"[ocr plugin] rapidocr failed: {exc}", file=sys.stderr)
        return None

    # rapidocr >= 3 returns a RapidOCROutput object with .txts;
    # older versions return (result, elapse) with result = [[box, text, score], ...].
    txts = None
    if hasattr(out, "txts"):
        txts = out.txts
    elif isinstance(out, tuple) and out and isinstance(out[0], (list, tuple)):
        items = out[0]
        txts = []
        for item in items:
            if isinstance(item, (list, tuple)) and len(item) >= 2:
                txts.append(item[1])
            elif isinstance(item, dict):
                txts.append(item.get("text") or item.get("txt") or "")
    if not txts:
        return ""
    lines = [str(t).strip() for t in txts if str(t).strip()]
    return "\n".join(lines)


def recognize_tesseract(image):
    tesseract = shutil.which("tesseract")
    if not tesseract:
        print("[ocr plugin] tesseract not found either; install rapidocr "
              "(pip install rapidocr) or tesseract-ocr + tesseract-ocr-chi-sim",
              file=sys.stderr)
        return None
    try:
        proc = subprocess.run([tesseract, image, "stdout", "-l", "chi_sim+eng"],
                              capture_output=True, text=True, timeout=120)
    except Exception as exc:
        print(f"[ocr plugin] tesseract failed: {exc}", file=sys.stderr)
        return None
    return proc.stdout.strip() or ""


def check_support():
    """Exit 0 when RapidOCR is usable, 1 otherwise. Fast: imports only, no engine init."""
    try:
        import rapidocr  # noqa: F401
        import onnxruntime  # noqa: F401  # the plugin's default inference engine
        return 0
    except ImportError as exc:
        print(f"[ocr plugin] rapidocr unsupported: {exc}", file=sys.stderr)
        return 1


def main():
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

    if text:
        print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
