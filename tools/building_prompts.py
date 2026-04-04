#!/usr/bin/env python3
"""
Building Prompt Templates for BlockSmith

Provides structured prompts that feed the LLM exact functional requirements
(dimensions, door/window openings, floor slabs) alongside creative freedom
for architectural style. The cuboid() DSL is a natural fit for building
construction — walls, floors, and roofs ARE cuboids.
"""

import json
from typing import Optional


# ---------------------------------------------------------------------------
# System prompt override for building generation
# ---------------------------------------------------------------------------
BUILDING_SYSTEM_PROMPT = """\
# Voxel Building Generator

You generate buildings for a voxel game engine using a simple Python DSL.
Output RAW PYTHON CODE ONLY. No markdown, no backticks, no explanations.
Start immediately with `def create_model():`.

## Coordinate System
- **X**: Width (left/right). +X = East/Right.
- **Y**: Height (up). +Y = Up. Ground is Y=0.
- **Z**: Depth (front/back). -Z = North/Front (where the door is by default).
- **1 unit = 1 game block** (approximately 1 meter).

## The API
These helpers are pre-defined. Just call them — DO NOT redefine them.

**`cuboid(id, corner, size, **kwargs)`**
- `id`: Unique string ID
- `corner`: `[min_x, min_y, min_z]` — back-bottom-left corner
- `size`: `[width_x, height_y, depth_z]`
- Optional: `material="mat-name"`

**`group(id, pivot, **kwargs)`**
- `id`: Unique string ID
- `pivot`: `[x, y, z]` rotation anchor
- Optional: `parent="grp_id"`

## CRITICAL Building Rules

1. **Units are game blocks.** A wall that is 10 blocks wide = size X of 10. Match EXACTLY.
2. **Walls are 1 block thick.** Use cuboid size of 1 in the thickness dimension.
3. **Doors are GAPS.** Do NOT place a cuboid for the door — leave the opening empty.
   Split the wall into segments around the door opening.
4. **Windows are GAPS.** Same principle — split walls into segments around window openings.
5. **Floors are solid slabs.** 1 block thick, spanning the full interior.
6. **Interior must be EMPTY.** No furniture — it's added separately.
7. **Roof goes on top.** Can be flat (1-block slab), pitched, or creative.
8. **All coordinates are integers or simple fractions.** Walls start/end at whole numbers.
9. **Origin (0,0,0) is the front-left corner at ground level.** Building extends in +X, +Y, +Z.
10. **Material groups**: Use descriptive material names like "mat-wall", "mat-floor", "mat-roof",
    "mat-trim", "mat-accent". These are mapped to game materials after generation.

## Wall Construction Pattern

For a wall WITH a door opening (2 wide × 3 tall, centered):
```python
# Front wall (Z=0), total width=10, door at X=4..6, Y=0..3
cuboid("wall_front_left",  [0, 0, 0], [4, 5, 1], material="mat-wall")   # left of door
cuboid("wall_front_right", [6, 0, 0], [4, 5, 1], material="mat-wall")   # right of door
cuboid("wall_front_above", [4, 3, 0], [2, 2, 1], material="mat-wall")   # above door
```

For a wall WITH a window opening (2 wide × 2 tall, at height 2):
```python
# Side wall, window at Z=4..6, Y=2..4
cuboid("wall_side_left",   [0, 0, 0], [1, 5, 4], material="mat-wall")   # left of window
cuboid("wall_side_right",  [0, 0, 6], [1, 5, 4], material="mat-wall")   # right of window
cuboid("wall_side_below",  [0, 0, 4], [1, 2, 2], material="mat-wall")   # below window
cuboid("wall_side_above",  [0, 4, 4], [1, 1, 2], material="mat-wall")   # above window
```

## Style & Creativity

While the functional structure must be exact, you have creative freedom for:
- **Roof design**: Flat, peaked, multi-level, overhanging, crenellated
- **Decorative elements**: Pillars, arches (stepped), trim bands, chimney
- **Asymmetric features**: Extended wings, porches, balconies (as solid platforms)
- **Foundation**: Raised base, steps leading to door
- Keep total cuboid count under 80 for buildings. Favor fewer, larger pieces.

## Example: Simple 8×5×10 House (door on front, 2 windows on sides)

```python
def create_model():
    return [
        # Floor
        cuboid("floor", [0, 0, 0], [8, 1, 10], material="mat-floor"),

        # Front wall (Z=0) with door at X=3..5, Y=1..4
        cuboid("wall_front_l", [0, 1, 0], [3, 4, 1], material="mat-wall"),
        cuboid("wall_front_r", [5, 1, 0], [3, 4, 1], material="mat-wall"),
        cuboid("wall_front_top", [3, 4, 0], [2, 1, 1], material="mat-wall"),

        # Back wall (Z=9)
        cuboid("wall_back", [0, 1, 9], [8, 4, 1], material="mat-wall"),

        # Left wall (X=0) with window at Z=4..6, Y=2..4
        cuboid("wall_left_a", [0, 1, 1], [1, 4, 3], material="mat-wall"),
        cuboid("wall_left_b", [0, 1, 6], [1, 4, 3], material="mat-wall"),
        cuboid("wall_left_below", [0, 1, 4], [1, 1, 2], material="mat-wall"),
        cuboid("wall_left_above", [0, 4, 4], [1, 1, 2], material="mat-wall"),

        # Right wall (X=7) with window at Z=4..6, Y=2..4
        cuboid("wall_right_a", [7, 1, 1], [1, 4, 3], material="mat-wall"),
        cuboid("wall_right_b", [7, 1, 6], [1, 4, 3], material="mat-wall"),
        cuboid("wall_right_below", [7, 1, 4], [1, 1, 2], material="mat-wall"),
        cuboid("wall_right_above", [7, 4, 4], [1, 1, 2], material="mat-wall"),

        # Roof (flat with slight overhang)
        cuboid("roof", [-1, 5, -1], [10, 1, 12], material="mat-roof"),
    ]
```
"""


# ---------------------------------------------------------------------------
# Prompt builder
# ---------------------------------------------------------------------------

def build_building_prompt(
    building_type: str,
    style: str,
    width: int,
    depth: int,
    height: int,
    stories: int = 1,
    door_wall: str = "front",
    windows_per_wall: int = 2,
    materials: Optional[dict] = None,
    extra_notes: str = "",
) -> str:
    """
    Build a structured user prompt for building generation.

    Args:
        building_type: e.g. "tavern", "house", "tower", "shop", "temple"
        style: e.g. "medieval", "elven", "dwarven", "gothic", "rustic"
        width: Building width in blocks (X axis)
        depth: Building depth in blocks (Z axis)
        height: Interior height per story in blocks (Y axis, excluding floor slab)
        stories: Number of stories
        door_wall: Which wall has the door ("front"=Z=0, "back", "left", "right")
        windows_per_wall: Number of windows per side wall per story
        materials: Optional dict with keys wall/floor/roof mapping to game materials
        extra_notes: Additional creative direction

    Returns:
        Formatted user prompt string
    """
    total_height = stories * (height + 1)  # +1 for each floor slab
    door_width = 2
    door_height = 3

    # Material description
    mat_desc = ""
    if materials:
        mat_desc = "\nMaterial mapping (use these mat- prefixes):\n"
        for role, game_mat in materials.items():
            mat_desc += f"  - mat-{role} → {game_mat}\n"

    # Multi-story instructions
    story_instructions = ""
    if stories > 1:
        story_instructions = f"""
MULTI-STORY REQUIREMENTS:
- Build {stories} stories, each with {height} blocks of interior height.
- Place a 1-block-thick floor slab at Y=0 (ground) and at the base of each upper story.
- Leave a 3×3 stairwell opening in the back-right corner of each floor (except the roof).
  The stairwell opening should be at X=(width-4)..(width-1), Z=(depth-4)..(depth-1).
- Upper story floor slabs span the full width/depth EXCEPT the stairwell opening.
"""

    # Window instructions
    window_instructions = ""
    if windows_per_wall > 0:
        window_instructions = f"""
WINDOW OPENINGS:
- Place {windows_per_wall} window(s) per side wall per story.
- Each window is 2 blocks wide × 2 blocks tall.
- Windows start at Y=2 above each floor level (leave 1 block of wall below).
- Space windows evenly along the wall length.
- Windows are GAPS — split the wall into segments around each opening.
"""

    # Building-type-specific notes
    type_notes = {
        "tavern": "This is a tavern/pub. Consider: a large open main hall on the ground floor, "
                  "a prominent entrance, thick sturdy walls. The upper floor (if multi-story) "
                  "has smaller rooms for sleeping quarters.",
        "house": "This is a residential house. Consider: a cozy entrance, living space on the "
                 "ground floor, bedrooms upstairs if multi-story.",
        "tower": "This is a watchtower or wizard tower. Consider: a circular or octagonal "
                 "footprint approximated with cuboids, narrow windows (1×2), battlements or "
                 "parapet on top.",
        "shop": "This is a merchant shop. Consider: a wide storefront window on the front wall, "
                "a counter area visible from outside.",
        "temple": "This is a temple or shrine. Consider: tall interior, columns or pillars, "
                  "an altar area at the back, possibly stained glass (tall narrow windows).",
        "barn": "This is a barn or warehouse. Consider: large double door (4 wide × 4 tall), "
                "no windows or small high windows, peaked roof.",
    }
    building_notes = type_notes.get(building_type, f"This is a {building_type}.")

    prompt = f"""\
Generate a {style} {building_type} building.

EXACT DIMENSIONS (in game blocks, 1 block = 1 unit):
- Width (X axis): {width} blocks
- Depth (Z axis): {depth} blocks
- Interior height per story (Y axis): {height} blocks (wall height above each floor)
- Number of stories: {stories}
- Total building height: ~{total_height} blocks (including floor slabs + roof)

DOOR:
- Position: Centered on the {door_wall} wall (Z=0 side)
- Size: {door_width} blocks wide × {door_height} blocks tall
- The door is a GAP — do not place a cuboid there. Split the wall around it.

{window_instructions}
{story_instructions}

STRUCTURE REQUIREMENTS:
- Ground floor slab at Y=0, 1 block thick, spanning full {width}×{depth}
- All walls are 1 block thick
- Interior is EMPTY (no furniture, tables, etc. — added separately)
- Roof on top — be creative with the roof style (flat, peaked, etc.)
- Origin (0,0,0) is at the front-left corner at ground level

BUILDING CHARACTER:
{building_notes}
{extra_notes}

STYLE: {style}
- Add architectural character appropriate to the style
- Use mat-wall for walls, mat-floor for floors, mat-roof for roof
- Use mat-trim or mat-accent for decorative elements
- Keep total cuboid count under 80
{mat_desc}"""

    return prompt


# ---------------------------------------------------------------------------
# Material name mapping
# ---------------------------------------------------------------------------

# Map from mat-* names used in DSL to game material names
DEFAULT_MATERIAL_MAP = {
    "mat-wall": "Stone",
    "mat-floor": "Wood",
    "mat-roof": "Wood",
    "mat-trim": "Wood",
    "mat-accent": "Stone",
    "mat-door": "Wood",
    "mat-window": "Glass",
    "mat-chimney": "Stone",
    "mat-foundation": "Stone",
}


def make_material_map(materials: Optional[dict] = None) -> dict:
    """
    Build a mat-* → game material mapping from user-supplied palette.

    Args:
        materials: Dict like {"wall": "Stone", "floor": "Wood", "roof": "Wood"}

    Returns:
        Dict mapping "mat-wall" → "Stone", etc.
    """
    mat_map = dict(DEFAULT_MATERIAL_MAP)
    if materials:
        for role, game_mat in materials.items():
            mat_map[f"mat-{role}"] = game_mat
    return mat_map
