"""Remove explicitly named clips and their metadata from a Phyxel .anim file."""

import argparse
from pathlib import Path

from anim_format import parse, write


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("anim", type=Path)
    parser.add_argument("clips", nargs="+")
    args = parser.parse_args()

    anim = parse(args.anim)
    missing = [name for name in args.clips if anim.clip(name) is None]
    if missing:
        raise SystemExit(f"Missing clips: {', '.join(missing)}")
    for name in args.clips:
        anim.remove_clip(name)
        anim.remove_clip_meta(name)
        print(f"Removed {name}")
    write(anim, args.anim)


if __name__ == "__main__":
    main()
