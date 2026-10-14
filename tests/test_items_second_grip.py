"""A3 item 3: `held.secondGrip` is DERIVED from the template manifest for two-handed / staff /
polearm items by tools/items_second_grip.py and shipped in resources/items.json.
RED 2026-09-30: no item carries a secondGrip."""
from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import items_second_grip as isg  # noqa: E402


def _items():
    data = json.loads((ROOT / "resources/items.json").read_text(encoding="utf-8"))
    items = data["items"] if isinstance(data, dict) and "items" in data else data
    return {i["id"]: i for i in items}


def test_two_handed_items_ship_a_second_grip_and_one_handed_ones_do_not():
    items = _items()
    for two_handed in ("maul", "staff_fire", "spear", "battle_axe"):
        held = items[two_handed].get("held", {})
        assert "secondGrip" in held, f"{two_handed}: run python tools/items_second_grip.py --write"
        sg = held["secondGrip"]
        assert len(sg) == 3 and all(isinstance(v, (int, float)) for v in sg)
    for one_handed in ("iron_sword", "dagger", "warhammer", "torch"):
        assert "secondGrip" not in items[one_handed].get("held", {}), one_handed


def test_second_grip_sits_a_hand_span_up_the_shaft_inside_the_template():
    manifest = json.loads((ROOT / "resources/templates/items_manifest.json").read_text(encoding="utf-8"))
    items = _items()
    for item_id, stem in (("maul", "maul"), ("staff_fire", "staff_fire"), ("spear", "spear")):
        sg = items[item_id]["held"]["secondGrip"]
        grip = manifest[stem]["grip_point_units"]
        dims = manifest[stem]["dims_units"]
        assert abs(sg[0] - grip[0]) < 1e-3 and abs(sg[2] - grip[2]) < 1e-3, item_id
        assert grip[1] + 0.05 < sg[1] <= dims[1] - isg.END_MARGIN_UNITS + 1e-6, item_id
        assert sg[1] - grip[1] <= isg.HAND_SPAN_UNITS + 1e-6, item_id


def test_derivation_is_a_pure_function_of_the_manifest_entry():
    span = isg.HAND_SPAN_UNITS
    assert isg.derive({"grip_point_units": [0.1, 0.2, 0.05], "dims_units": [0.3, 1.5, 0.1]}) == [0.1, round(0.2 + span, 4), 0.05]
    assert isg.derive({"grip_point_units": [0.1, 0.2, 0.05], "dims_units": [0.3, 0.28, 0.1]}) is None, "too short"
    assert isg.derive({"grip_point_units": [0.1, 1.4, 0.05], "dims_units": [0.3, 1.5, 0.1]}) == [0.1, 1.45, 0.05], "clamped to the top"
