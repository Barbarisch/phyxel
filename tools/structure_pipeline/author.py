"""
Structure Pipeline P2 — LLM spec author.

Turns a natural-language description into a *validated* BuildingSpec: the LLM does the
spatial/semantic design, the P0 validator + scale canon guarantee correctness, and a
repair loop feeds validation errors back to the LLM until the spec is clean (or rounds
run out). This is the "LLM + deterministic" combination from docs/structure-generation/StructureGenerationPipeline.md.

The LLM call is pluggable (`LLMFn = (system, user) -> text`) so the prompt and repair loop
are testable without API calls. The default backend calls the Anthropic Messages API directly
via stdlib urllib (key from ANTHROPIC_API_KEY / PHYXEL_AI_API_KEY).
"""

from __future__ import annotations

import json
import os
import re
import urllib.request
from dataclasses import dataclass, field
from typing import Callable, List, Optional

from .scale import ScaleCanon, load_canon
from .spec import BuildingSpec, BUILDING_FUNCTIONS
from .validator import validate_dict, ValidationReport
from .playtest import full_validate_dict

# (system_prompt, user_prompt) -> assistant text
LLMFn = Callable[[str, str], str]

DEFAULT_MODEL = "claude-sonnet-4-6"

# Material names the realizer/MaterialRegistry understands (subset of resources/materials.json).
_MATERIALS = ("Stone Cobblestone StoneBricks Bricks Sandstone Wood Log Dirt Grass Sand "
              "Gravel Glass Metal Gold").split()


# --------------------------------------------------------------------------- prompts

def build_system_prompt(canon: ScaleCanon) -> str:
    """The instructional prompt: teaches the BuildingSpec schema, scale canon, and the exact
    functional rules the validator enforces, so the model produces valid specs first try."""
    c = canon
    materials = ", ".join(_MATERIALS)
    return f"""\
You are an architect that designs FUNCTIONAL voxel buildings as a strict JSON `BuildingSpec`.
You output the floorplan/program; deterministic code turns it into voxels with working doors.

# Units & coordinates
- 1 cube = 1 metre. Everything is in whole cubes. Y is up.
- A `rect` is `[x, z, w, d]`: the min corner (x,z) + size (w,d) of an XZ footprint.
- A portal `pos` is `[x, z]`: the MIN CORNER of the opening; the opening runs `width` cubes
  along its wall from there.

# Scale (a person is {c.character_height:.2f} cubes tall — size to this)
- Interior story height >= {c.ceiling_min} (hard min), {c.ceiling_comfortable}+ is comfortable.
- GRAND buildings (mansion, manor, church, tavern, great hall, keep): make them feel GRAND, not cramped —
  interior height 4-5 cubes, and large rooms (main/public rooms 8-12 cubes per side, even bedrooms 7-9). A
  cramped grand house is a failure. Humble homes/shops/cottages can stay at height {c.ceiling_comfortable} with
  5-7 cube rooms. Pick a footprint big enough for this (a manor wants ~26-36 per side, not 16-18).
- Door/arch openings: height EXACTLY {c.door_clear_min}, width {c.door_width_min} for a normal single door
  (use width 2 for grand double doors / main entrances / barns). Windows: height 2 (the realizer sets them on a sill).

# Schema
{{
  "kind": "building", "name": "<short>", "style": "<e.g. medieval>",
  "palette": {{ "wall": "<mat>", "floor": "<mat>", "roof": "<mat>", "trim": "<mat>" }},
  "function": "house|shop|church|tavern|tower|stadium",
  "footprint": [W, D],
  "stories": [
    {{
      "height": <interior cubes>,
      "rooms":   [{{ "id": "<unique>", "rect": [x,z,w,d], "purpose": "<e.g. main_hall>", "floor_mat": "<mat>" }}],
      "portals": [{{ "between": ["roomId" | "exterior", "roomId" | "exterior"],
                    "pos": [x,z], "width": W, "height": H, "kind": "door|arch|window",
                    "door": {{ "lockable": true|false, "key": "<item id or ''>", "swing": 90 }} }}],
      "stairs":  [{{ "from_story": i, "to_story": i+1, "rect": [x,z,w,d], "kind": "straight|spiral" }}],
      "fixtures":[{{ "type": "<fixture type>", "rect": [x,z,w,d], "facing": "north|east|south|west", "room": "<roomId>" }}]
    }}
  ],
  "roof": {{ "style": "pitched|flat", "mat": "<mat>" }}
}}
Materials (use these names exactly): {materials}.

# Functional rules (a spec that breaks these is REJECTED)
1. MASSING — do NOT default to a solid rectangle; a flat box is the WRONG answer. `footprint` [W,D] is only the
   MAXIMUM bounding extent, NOT a fill target: it is good and EXPECTED for large parts of it to stay empty (open
   yard / courtyard). NEVER invent filler rooms just to complete the rectangle. Compose the rooms into an
   articulated L / T / U / cross / E / wing plan with courtyards, notches and projecting wings. The walls and
   roof automatically follow the union OUTLINE of whatever rooms you place; everything you leave out becomes
   open ground. A plan where the rooms happen to tile the whole bounding box is almost always a design mistake.
   Worked U (footprint [18,16]): a front block [0,0,18,6] (split into 3 rooms) + a west wing [0,6,6,10] + an
   east wing [12,6,6,10], leaving the center-rear [6,6,6,10] EMPTY as a courtyard. That is the level of
   articulation to aim for — wings and a courtyard, not a packed rectangle.
2. Rooms touch along shared walls, never OVERLAP, and stay inside [0,W]x[0,D]. Adjacent rooms share a grid line
   (e.g. roomA z 0..7 and roomB z 7..12 share the wall at z=7).
3. Every room must be REACHABLE from "exterior" through passable portals (kind door or arch; windows are NOT
   passable) plus stairs. Include at least ONE exterior door/arch (the entrance).
4. An interior portal's two rooms MUST be adjacent (share a wall), and the opening (pos + width) must lie ON
   that shared wall. An exterior portal's pos must be on the building perimeter (x=0 or x=W or z=0 or z=D).
5. Multi-story: each upper room reached via a `stairs` whose rect sits inside a room on BOTH stories;
   from_story/to_story are consecutive. An upper story may be SMALLER than the ground floor (sit over only part
   of the plan / some wings) — this is encouraged for varied massing.
6. Pick `function`-appropriate rooms + fixtures (shop: storefront + counter + back storeroom with a lockable
   door; church: long nave + altar + pews; house: living + bedroom(s); tavern: hall + bar + upstairs rooms;
   mansion: entry hall + dining + drawing room + kitchen/servants in the wings + bedrooms upstairs, around a
   rear courtyard).
7. FIXTURE TYPES: table chair stool bench bed counter bar altar pew barrel desk wardrobe dresser fireplace
   bookshelf shelf, and CLUTTER (books candlestick goblet bottle plate). Use them to furnish richly:
   - casegoods/shelving (bookshelf, shelf, wardrobe, dresser, fireplace) go FLUSH against a wall (they back
     onto it). A study/library is lined with bookshelves; bedrooms get a wardrobe/dresser + a bed.
   - CLUTTER sits ON a surface: give it the SAME rect as the table/desk/shelf it rests on (a candlestick or
     books on a desk, a goblet/bottle/plate on a dining table). Never place clutter on bare floor.
   - LIGHT: every room needs a light source. A window or exterior door counts (daylight); a windowless
     interior room (cellar, inner hall, landing, study) MUST get a light fixture — candlestick/candelabra on a
     table, a fireplace against a wall, or a sconce/torch. Don't leave any room pitch black.
   - Furniture must leave a walkable path: don't block a doorway or wall a room in two with a counter/table.
7. CLEARANCE (a person must be able to USE the building). Keep every doorway clear: NEVER place a fixture on a
   door threshold or in the cells just inside a door — a fixture there seals the room off. Keep each fixture
   fully inside its room with a walkable path around it, and leave a clear route from the entrance into every
   room. Stairs need run (Z-depth) >= the floor-to-floor climb, plus a landing in the room above.

# Output
Return RAW JSON only — the BuildingSpec object, no markdown, no commentary."""


def build_user_prompt(description: str,
                      function: Optional[str] = None,
                      footprint: Optional[list] = None,
                      stories: Optional[int] = None,
                      style: Optional[str] = None,
                      extra: str = "") -> str:
    lines = [f"Design: {description}"]
    if function:  lines.append(f"function: {function}")
    if style:     lines.append(f"style: {style}")
    if footprint: lines.append(f"footprint (W x D in cubes): {footprint[0]} x {footprint[1]}")
    if stories:   lines.append(f"stories: {stories}")
    if extra:     lines.append(extra)
    lines.append("Return the complete BuildingSpec JSON.")
    return "\n".join(lines)


# --------------------------------------------------------------------------- JSON extraction

def extract_json(text: str) -> dict:
    """Pull the BuildingSpec object out of an LLM response (handles ``` fences + prose)."""
    fence = re.search(r"```(?:json)?\s*(\{.*\})\s*```", text, re.DOTALL)
    if fence:
        return json.loads(fence.group(1))
    start = text.find("{")
    if start < 0:
        raise ValueError("no JSON object found in LLM response")
    depth = 0
    in_str = False
    esc = False
    for i in range(start, len(text)):
        ch = text[i]
        if in_str:
            if esc:        esc = False
            elif ch == "\\": esc = True
            elif ch == '"':  in_str = False
            continue
        if ch == '"':   in_str = True
        elif ch == "{": depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return json.loads(text[start:i + 1])
    raise ValueError("unbalanced JSON object in LLM response")


# --------------------------------------------------------------------------- LLM backend

def anthropic_llm(model: str = DEFAULT_MODEL, max_tokens: int = 4000,
                  api_key: Optional[str] = None, timeout: float = 120.0) -> LLMFn:
    """Default LLM backend: Anthropic Messages API via stdlib urllib."""
    key = api_key or os.environ.get("ANTHROPIC_API_KEY") or os.environ.get("PHYXEL_AI_API_KEY")
    if not key:
        raise RuntimeError("Set ANTHROPIC_API_KEY or PHYXEL_AI_API_KEY to author specs.")

    def call(system: str, user: str) -> str:
        body = json.dumps({
            "model": model, "max_tokens": max_tokens, "system": system,
            "messages": [{"role": "user", "content": user}],
        }).encode("utf-8")
        req = urllib.request.Request(
            "https://api.anthropic.com/v1/messages", data=body,
            headers={"x-api-key": key, "anthropic-version": "2023-06-01",
                     "content-type": "application/json"})
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            data = json.loads(resp.read())
        return "".join(b.get("text", "") for b in data.get("content", []) if b.get("type") == "text")

    return call


# --------------------------------------------------------------------------- authoring loop

@dataclass
class AuthorResult:
    spec: dict                          # the (best) BuildingSpec dict produced
    report: ValidationReport            # validation of `spec`
    rounds: int                         # repair rounds used (0 = valid first try)
    transcript: List[str] = field(default_factory=list)  # raw LLM responses

    @property
    def ok(self) -> bool:
        return self.report.ok


def author_spec(description: str,
                llm: Optional[LLMFn] = None,
                canon: Optional[ScaleCanon] = None,
                max_repair: int = 3,
                **user_kwargs) -> AuthorResult:
    """Author a validated BuildingSpec from a description, repairing on validation errors."""
    canon = canon or load_canon()
    llm = llm or anthropic_llm()
    system = build_system_prompt(canon)
    user = build_user_prompt(description, **user_kwargs)

    transcript: List[str] = []
    text = llm(system, user)
    transcript.append(text)
    spec = extract_json(text)
    report = full_validate_dict(spec, canon)

    rounds = 0
    while not report.ok and rounds < max_repair:
        rounds += 1
        errs = "\n".join(f"- {i}" for i in report.errors)
        repair_user = (
            f"{user}\n\nYour previous attempt:\n{json.dumps(spec)}\n\n"
            f"It FAILED validation with these errors:\n{errs}\n\n"
            "Return a corrected, COMPLETE BuildingSpec JSON (raw JSON only). "
            "Fix every error while keeping the design intent.")
        text = llm(system, repair_user)
        transcript.append(text)
        spec = extract_json(text)
        report = full_validate_dict(spec, canon)

    return AuthorResult(spec=spec, report=report, rounds=rounds, transcript=transcript)


# --------------------------------------------------------------------------- CLI

def main(argv=None) -> int:
    import argparse
    import sys
    from pathlib import Path

    ap = argparse.ArgumentParser(
        prog="structure_pipeline.author",
        description="Author a validated BuildingSpec from a natural-language description.")
    ap.add_argument("description", help='e.g. "a small medieval blacksmith shop"')
    ap.add_argument("--function", help="house|shop|church|tavern|tower|stadium")
    ap.add_argument("--style", help="e.g. medieval")
    ap.add_argument("--footprint", help="WxD in cubes, e.g. 10x12")
    ap.add_argument("--stories", type=int)
    ap.add_argument("--model", default=DEFAULT_MODEL)
    ap.add_argument("--max-repair", type=int, default=3)
    ap.add_argument("--out", type=Path, help="write the spec JSON here")
    ap.add_argument("--build", action="store_true",
                    help="POST the spec to a running engine at localhost:8090")
    ap.add_argument("--position", help="x,y,z world position for --build (default 0,16,0)")
    args = ap.parse_args(argv)

    fp = None
    if args.footprint:
        w, d = args.footprint.lower().split("x")
        fp = [int(w), int(d)]

    res = author_spec(args.description, llm=anthropic_llm(args.model),
                      max_repair=args.max_repair, function=args.function,
                      style=args.style, footprint=fp, stories=args.stories)

    print(f"[author] rounds={res.rounds}  {res.report.summary()}", file=sys.stderr)
    if args.out:
        args.out.write_text(json.dumps(res.spec, indent=2), encoding="utf-8")
    print(json.dumps(res.spec, indent=2))

    if args.build and res.ok:
        body = dict(res.spec)
        x, y, z = (args.position.split(",") if args.position else ("0", "16", "0"))
        body["position"] = {"x": int(x), "y": int(y), "z": int(z)}
        req = urllib.request.Request(
            "http://localhost:8090/api/structure/build",
            data=json.dumps(body).encode("utf-8"),
            headers={"Content-Type": "application/json"})
        try:
            print("[build]", urllib.request.urlopen(req, timeout=40).read().decode(), file=sys.stderr)
        except Exception as e:
            print(f"[build] FAILED (is the engine running?): {e}", file=sys.stderr)

    return 0 if res.ok else 1


if __name__ == "__main__":
    import sys
    sys.exit(main())
