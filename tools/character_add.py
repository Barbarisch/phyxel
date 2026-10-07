#!/usr/bin/env python3
"""Entry point for tools/characters/character_add.py (roadmap R2 decision 5)."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.characters.character_add import main  # noqa: E402

if __name__ == "__main__":
    sys.exit(main())
