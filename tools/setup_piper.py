#!/usr/bin/env python3
"""Download the Piper TTS binary + a multi-speaker voice model for NPC voices.

Phyxel uses local Piper TTS so NPC dialogue can be spoken with no cloud
dependency, no API keys, and no per-character cost. A single multi-speaker
model (en_US-libritts_r-medium, ~900 speakers) gives every NPC a distinct,
consistent voice via a procedurally-chosen speaker id.

Assets land in external/piper/ and are intentionally git-ignored (the model is
~80 MB). Run this once after cloning to enable NPC voices; the engine degrades
gracefully (no audio) when the assets are absent.

    python tools/setup_piper.py
"""
import os
import sys
import zipfile
import urllib.request
import shutil
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
PIPER_DIR = REPO_ROOT / "external" / "piper"
MODELS_DIR = PIPER_DIR / "models"

PIPER_WIN_URL = "https://github.com/rhasspy/piper/releases/download/2023.11.14-2/piper_windows_amd64.zip"
PIPER_LINUX_URL = "https://github.com/rhasspy/piper/releases/download/2023.11.14-2/piper_linux_x86_64.tar.gz"

MODEL_BASE = "https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/libritts_r/medium"
MODEL_ONNX = "en_US-libritts_r-medium.onnx"
MODEL_JSON = "en_US-libritts_r-medium.onnx.json"


def _download(url: str, dest: Path) -> None:
    if dest.exists() and dest.stat().st_size > 0:
        print(f"  [skip] {dest.name} already present ({dest.stat().st_size:,} bytes)")
        return
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp = dest.with_suffix(dest.suffix + ".part")
    print(f"  [get ] {url}")

    def _progress(blocks, block_size, total):
        if total <= 0:
            return
        done = min(blocks * block_size, total)
        pct = 100.0 * done / total
        sys.stdout.write(f"\r         {done:,} / {total:,} bytes ({pct:5.1f}%)")
        sys.stdout.flush()

    urllib.request.urlretrieve(url, tmp, _progress)
    sys.stdout.write("\n")
    tmp.replace(dest)
    print(f"  [ok  ] {dest.name} ({dest.stat().st_size:,} bytes)")


def _extract_piper_binary() -> None:
    """Download + unpack the Piper engine binary for this platform."""
    if sys.platform.startswith("win"):
        archive = PIPER_DIR / "piper_windows_amd64.zip"
        _download(PIPER_WIN_URL, archive)
        if not (PIPER_DIR / "piper" / "piper.exe").exists() and not (PIPER_DIR / "piper.exe").exists():
            print("  [unzip] piper_windows_amd64.zip")
            with zipfile.ZipFile(archive) as zf:
                zf.extractall(PIPER_DIR)
    else:
        archive = PIPER_DIR / "piper_linux_x86_64.tar.gz"
        _download(PIPER_LINUX_URL, archive)
        if not (PIPER_DIR / "piper" / "piper").exists():
            print("  [untar] piper_linux_x86_64.tar.gz")
            import tarfile
            with tarfile.open(archive) as tf:
                tf.extractall(PIPER_DIR)


def main() -> int:
    print(f"Setting up Piper TTS in {PIPER_DIR}")
    PIPER_DIR.mkdir(parents=True, exist_ok=True)

    _extract_piper_binary()
    _download(f"{MODEL_BASE}/{MODEL_ONNX}", MODELS_DIR / MODEL_ONNX)
    _download(f"{MODEL_BASE}/{MODEL_JSON}", MODELS_DIR / MODEL_JSON)

    # Locate the extracted binary for a sanity message.
    candidates = [PIPER_DIR / "piper" / "piper.exe",
                  PIPER_DIR / "piper.exe",
                  PIPER_DIR / "piper" / "piper"]
    binary = next((c for c in candidates if c.exists()), None)
    print()
    if binary:
        print(f"Piper binary: {binary}")
    else:
        print("WARNING: piper binary not found after extraction — check the archive layout.")
    print(f"Voice model:  {MODELS_DIR / MODEL_ONNX}")
    print("Done. NPC voices will activate the next time the engine launches.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
