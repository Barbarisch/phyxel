"""Remove rejected procedural sword clips from every generated character anim."""

from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from anim_format import parse, write  # noqa: E402


ROOT = Path(__file__).resolve().parents[2]
ANIM_DIR = ROOT / "resources" / "animated_characters"
REJECTED = ("sword1h_guard", "sword1h_prototype")


def main() -> None:
    changed = 0
    for path in sorted(ANIM_DIR.glob("*.anim")):
        anim = parse(path)
        removed = []
        for name in REJECTED:
            clip_removed = anim.remove_clip(name)
            meta_removed = anim.remove_clip_meta(name)
            if clip_removed or meta_removed:
                removed.append(name)
        if not removed:
            continue
        write(anim, path)
        changed += 1
        print(f"{path.name}: removed {', '.join(removed)}")
    print(f"Cleaned {changed} animation files")


if __name__ == "__main__":
    main()
