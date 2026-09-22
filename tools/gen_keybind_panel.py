#!/usr/bin/env python3
"""gen_keybind_panel.py - rebuild the Settings > Keybindings panel from the engine's
default binding table.

WHY GENERATED. The panel is hand-authored absolute positions, and the action list lives in
C++ (`GameSettings::defaultKeybindings()`). They drifted: four live actions - StrafeLeft,
StrafeRight, ToggleAutorun, ToggleWalk - existed in the table but had no row, so they could
only be changed by hand-editing settings.json. Generating the panel from the table makes
that impossible, and `GameSettingsTest.EveryDefaultBindingIsEditableInTheSettingsPanel`
keeps it honest if someone edits the JSON directly.

  python tools/gen_keybind_panel.py           # rewrite the panel
  python tools/gen_keybind_panel.py --check   # fail if it is out of date
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULTS = os.path.join(REPO, "engine", "src", "core", "GameSettings.cpp")
SCREEN = os.path.join(REPO, "resources", "ui", "settings_screen.json")

# Human labels. An action with no entry falls back to spaced-out CamelCase, so a new
# binding still gets a readable row without touching this table.
LABELS = {
    "MoveForward": "Move Forward", "MoveBackward": "Move Backward",
    "MoveLeft": "Move Left", "MoveRight": "Move Right",
    "StrafeLeft": "Strafe Left", "StrafeRight": "Strafe Right",
    "Jump": "Jump", "Sprint": "Sprint", "Crouch": "Crouch",
    "ToggleAutorun": "Autorun", "ToggleWalk": "Walk / Run",
    "TogglePause": "Pause", "ToggleInventory": "Inventory",
    "ToggleCharacter": "Character", "ToggleAbilities": "Abilities Panel",
    "Attack": "Attack", "Interact": "Interact",
}
for i in range(1, 13):
    LABELS["ActionSlot%d" % i] = "Action Bar %d" % i

# Reading order down each column, so related bindings sit together.
ORDER = [
    "MoveForward", "MoveBackward", "MoveLeft", "MoveRight",
    "StrafeLeft", "StrafeRight", "Jump", "Sprint", "Crouch", "ToggleAutorun",
    "ToggleWalk", "Interact", "Attack", "TogglePause", "ToggleInventory",
    "ToggleCharacter", "ToggleAbilities",
] + ["ActionSlot%d" % i for i in range(1, 13)]

# TWO columns of up to 15 rows on the 1280x720 canvas. The first attempt used three
# columns with a 180 px label and a 150 px button, and the capture showed why that is
# wrong: a label wraps at its own width, so "Move Backward", "Abilities Panel" and
# "Action Bar 10" wrapped to two lines and collided with the row beneath, and
# "NumpadDivide" was wider than its button and spilled over the neighbouring label.
# Sized from the real text instead: the longest label ("Abilities Panel", 15 chars) and
# the longest key name ("NumpadDivide", 12) at ~14.6 px/char in the HUD font.
ROWS_PER_COL = 15
COL_X = [160, 700]
ROW_Y0, ROW_PITCH = 150, 36
LABEL_W, BTN_W, BTN_DX = 240, 190, 250


def read_default_actions():
    """Action names from GameSettings::defaultKeybindings(), in declaration order."""
    src = open(DEFAULTS, encoding="utf-8").read()
    body = src[src.index("GameSettings::defaultKeybindings()"):]
    body = body[:body.index("};")]
    return re.findall(r'\{"([A-Za-z0-9_]+)",', body)


def pretty(action):
    return LABELS.get(action, re.sub(r"(?<!^)(?=[A-Z])", " ", action))


def build_children(actions):
    ordered = [a for a in ORDER if a in actions] + [a for a in actions if a not in ORDER]
    if len(ordered) > ROWS_PER_COL * len(COL_X):
        raise SystemExit("%d actions exceed the %d-row x %d-column panel - widen it"
                         % (len(ordered), ROWS_PER_COL, len(COL_X)))
    out = [
        {"type": "label", "align": "center", "id": "kb_title", "text": "KEYBINDINGS",
         "font": "title", "position": [640, 36], "size": [400, 60]},
        {"type": "label", "align": "center", "id": "kb_hint",
         "text": "Click a key, then press the new key. ESC cancels.",
         "position": [640, 100], "size": [880, 26]},   # 700 wrapped onto row 1
    ]
    for i, action in enumerate(ordered):
        col, row = divmod(i, ROWS_PER_COL)
        x, y = COL_X[col], ROW_Y0 + row * ROW_PITCH
        out.append({"type": "label", "id": "lbl_" + action, "text": pretty(action),
                    "position": [x, y], "size": [LABEL_W, 30]})
        out.append({"type": "button", "id": "kb_" + action,
                    "text": "{{keybind.%s}}" % action,
                    "position": [x + BTN_DX, y - 4], "size": [BTN_W, 34],
                    "action": {"type": "rebind", "binding": action}})
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="do not write; fail if the panel is out of date")
    args = ap.parse_args()

    actions = read_default_actions()
    if not actions:
        print("could not read defaultKeybindings() from %s" % DEFAULTS, file=sys.stderr)
        return 1
    want = build_children(actions)

    screen = json.load(open(SCREEN, encoding="utf-8-sig"))
    have = screen["panels"]["keybindings"]["children"]

    if args.check:
        if have == want:
            print("keybindings panel is up to date (%d actions)" % len(actions))
            return 0
        listed = {c["action"]["binding"] for c in have
                  if c.get("action", {}).get("type") == "rebind"}
        missing = [a for a in actions if a not in listed]
        extra = [a for a in listed if a not in actions]
        print("keybindings panel is OUT OF DATE - run: python tools/gen_keybind_panel.py")
        if missing:
            print("  no row for: %s" % ", ".join(missing))
        if extra:
            print("  row for an action that is not a default: %s" % ", ".join(extra))
        if not missing and not extra:
            print("  same actions, different layout")
        return 1

    screen["panels"]["keybindings"]["children"] = want
    # The file is written with a BOM upstream; keep it, so this is a content-only diff.
    with open(SCREEN, "w", encoding="utf-8-sig") as f:
        json.dump(screen, f, indent=2)
        f.write("\n")
    print("keybindings panel: %d actions in %d columns" % (len(actions), len(COL_X)))
    for a in actions:
        print("   %-18s %s" % (a, pretty(a)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
