"""
CLI: validate a BuildingSpec JSON against the character scale canon.

    python -m structure_pipeline path/to/spec.json [--archetype humanoid_normal]

Prints the validation report and exits non-zero if the spec has errors.
"""

import argparse
import sys
from pathlib import Path

from .scale import load_canon
from .validator import validate_file


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="structure_pipeline", description=__doc__)
    ap.add_argument("spec", type=Path, help="Path to a BuildingSpec JSON file")
    ap.add_argument("--archetype", default="humanoid_normal", help="Scale-canon archetype")
    ap.add_argument("--canon", type=Path, default=None, help="Override canon JSON path")
    args = ap.parse_args(argv)

    canon = load_canon(args.canon, args.archetype)
    rep = validate_file(args.spec, canon)
    print(f"Scale canon: {canon.archetype} (character {canon.character_height:.3f} cubes, "
          f"ceiling_min {canon.ceiling_min}, door_clear_min {canon.door_clear_min})")
    print(rep.summary())
    return 0 if rep.ok else 1


if __name__ == "__main__":
    sys.exit(main())
