"""
Prompt enhancer for Phyxel voxel generation.

Runs a pre-pass LLM call that expands a terse user prompt into a spatially
detailed, material-annotated description ready for PhyxelGenerator. When an
image is provided, the LLM analyzes the 3D structure from the image first.
"""

from __future__ import annotations

import os
import sys
from typing import Optional

# Ensure blocksmith is importable
_repo_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
_blocksmith_path = os.path.join(_repo_root, "external", "blocksmith")
if _blocksmith_path not in sys.path:
    sys.path.insert(0, _blocksmith_path)

from blocksmith.llm.client import LLMClient


_ENHANCER_SYSTEM_PROMPT = """\
You are a 3D modeling consultant helping prepare descriptions for a voxel model generator.

Your job is to expand a terse user prompt into a detailed spatial breakdown that a voxel LLM
can use to produce an accurate, well-proportioned model.

## Output Format

Return ONLY a revised prompt in plain text. No JSON, no headers, no explanations outside the prompt.
The revised prompt should be written as a direct instruction to the modeling LLM.

## Scale Reference — ALWAYS apply this

**1 cube = 1 meter.** The standard humanoid character is **~2 cubes tall**.
Every spatial dimension you specify must be grounded in this scale.

Humanoid-scale furniture reference heights:
- Chair seat surface: **0.67 cubes** (subcube sy=2 in cube Y=0)
- Throne seat surface: **0.67–1.0 cubes** (may have a small stone dais under it)
- Table / workbench top: **0.8 cubes**
- Door clear height: **2.2 cubes**
- Bed mattress: **0.3–0.5 cubes**

**Hard limits for humanoid furniture:**
- Chair total height ≤ 1.3 cubes
- Throne total height ≤ 3.0 cubes (backrest tall, but seat must be ≤ 1.0 cubes from ground)
- Table total height ≤ 1.0 cube
- NEVER place a seat surface above Y=1.0 for standard humanoid furniture

## What to include in the revised prompt

1. **Each major part** — name it, give its approximate dimensions in "world cubes" (1 cube ≈ 1 meter),
   describe its Y position (height from floor) using the scale reference above.
2. **Material for each part** — choose from: Wood, Metal, Glass, Rubber, Stone, Ice, Cork, Dirt, glow, Leaf, Default
3. **Detail level for each part** — one of:
   - "bulk" → will use full cubes (C) — for large structural volumes
   - "medium" → will use subcubes (S, 1/3 scale) — for surface features, panels, trim
   - "fine" → will use microcubes (M, 1/9 scale) — for small markings, rivets, stitching, buttons
4. **Overall size** — state the expected bounding box in world cubes. Always verify it matches the scale reference.
5. **Interaction point** — for seatable objects, specify seat surface Y and that IP localY = seat Y + 0.33

## If an image is provided

1. First analyze the image: describe the overall silhouette and major 3D volumes you can identify.
2. The image may be 2D — infer the missing depth dimension from context and symmetry.
3. Use visible textures to determine materials (wood grain → Wood, metal sheen → Metal, fabric → Rubber, etc.)
4. Then write the revised prompt based on your image analysis.

## Example

User input: "a wooden chair"

Revised prompt:
> Model a wooden chair sized for a standard humanoid (2m tall character).
> Bounding box: approximately 1 cube wide × 1.3 cubes tall × 1 cube deep.
>
> Parts:
> - 4 legs (Wood, medium): each a pair of S-column subcubes at corners, 0–0.67m tall (Y=0, subcube sy=0 and sy=1).
> - Seat cushion (Rubber, medium): 3×3 grid of S subcubes at sy=2 within cube Y=0, surface height = 0.67m.
> - Backrest frame (Wood, bulk): 1 cube wide, 1 cube tall, at Z=0 (back of seat), from cube Y=1.
>   Backrest top = 1.67m — just above a seated character's shoulders.
> - Backrest cushion (Rubber, medium): front subcube strip (sz=0) of the backrest cube.
> - Rivets (Metal, fine): M-primitives at backrest corner joints.
>
> Interaction point: seat type, seat surface at Y=0.67 → IP localY=1.0. Character faces -Z (away from backrest).
> The chair faces +Z (front toward viewer). Origin at base of front-left corner.
"""


def enhance_prompt(
    user_prompt: str,
    image: Optional[str] = None,
    model: Optional[str] = None,
) -> str:
    """
    Expand a terse user prompt into a spatially detailed description.

    Args:
        user_prompt: Short text description (e.g. "a wooden chair")
        image: Optional local file path or URL to a reference image
        model: LLM model to use (defaults to anthropic/claude-haiku-4-5 for cost)

    Returns:
        Revised prompt string ready for PhyxelGenerator
    """
    effective_model = model or os.environ.get("PHYXEL_ENHANCER_MODEL", "anthropic/claude-haiku-4-5")

    client = LLMClient(model=effective_model, temperature=0.3, max_tokens=2000)

    if image:
        user_text = (
            f"Reference image provided. Analyze the image first, then expand this prompt:\n\n{user_prompt}"
        )
    else:
        user_text = f"Expand this prompt into a spatial breakdown:\n\n{user_prompt}"

    user_content = LLMClient._build_multimodal_content(user_text, image)

    messages = [
        {"role": "system", "content": _ENHANCER_SYSTEM_PROMPT},
        {"role": "user", "content": user_content},
    ]

    response = client.complete(messages)
    return response.content.strip()


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="Test the prompt enhancer")
    parser.add_argument("prompt", help="Input prompt to enhance")
    parser.add_argument("--image", default=None, help="Reference image path or URL")
    parser.add_argument("--model", default=None, help="LLM model to use")
    cli_args = parser.parse_args()

    # Set up API key
    phyxel_key = os.environ.get("PHYXEL_AI_API_KEY")
    if phyxel_key and not os.environ.get("ANTHROPIC_API_KEY"):
        os.environ["ANTHROPIC_API_KEY"] = phyxel_key

    result = enhance_prompt(cli_args.prompt, image=cli_args.image, model=cli_args.model)
    print("=== Enhanced Prompt ===")
    print(result)
