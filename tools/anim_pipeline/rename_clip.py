"""Rename one clip and its clip_meta entry in a Phyxel .anim file."""

import argparse
from pathlib import Path

from anim_format import parse, write


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("anim", type=Path)
    parser.add_argument("old_name")
    parser.add_argument("new_name")
    args = parser.parse_args()

    anim = parse(args.anim)
    clip = anim.clip(args.old_name)
    if clip is None:
        raise SystemExit(f"Missing clip: {args.old_name}")
    if anim.clip(args.new_name) is not None:
        raise SystemExit(f"Target clip already exists: {args.new_name}")

    meta = anim.clip_meta(args.old_name)
    clip.name = args.new_name
    anim.remove_clip(args.old_name)
    anim.remove_clip_meta(args.old_name)
    anim.set_clip(clip)
    if meta is not None:
        anim.set_clip_meta(args.new_name, meta)
    write(anim, args.anim)
    print(f"Renamed {args.old_name} -> {args.new_name} in {args.anim}")


if __name__ == "__main__":
    main()
