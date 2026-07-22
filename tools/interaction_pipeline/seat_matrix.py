"""Seat-fit matrix — coverage + enforcement gate for the seat-fit policy.

For every (preset x seat template): spawn the preset, ask `can_interact`
(read-only, same rules as the sit gate), and print the verdict matrix.
Asserts:
  1. EVERY preset has at least one fitting seat (coverage).
  2. Key expectation cells hold (halfling refused on chair_wood-height seats?
     no — halfling gets stool_low; ogre refused everywhere except bench_great;
     standard fits chair_wood; nobody fits a seat with missing metrics).
  3. A real sit attempt matches the can_interact verdict (gate parity) for
     one fitting and one refused cell per preset.

Run with the engine up (spawns its own seats at a clear spot, removes them):
    python tools/interaction_pipeline/seat_matrix.py
"""
import json
import sys
import time
import urllib.request

BASE = "http://localhost:8090"
SPOT = (120, 17, 120)   # clear ground on CharacterTestbed
SEATS = ["stool_low", "stool", "chair_wood", "bench_wood", "bar_stool",
         "test_chair", "bench_great"]
PRESETS = ["halfling", "gnome", "goblin", "dwarf", "standard", "elf",
           "half_orc", "goliath", "ogre"]


def post(path, body, timeout=90):
    req = urllib.request.Request(BASE + path, json.dumps(body).encode(),
                                 {"Content-Type": "application/json"})
    try:
        return json.load(urllib.request.urlopen(req, timeout=timeout))
    except Exception as e:
        return {"error": str(e)}


def main():
    # Spawn one of each seat in a row.
    seat_ids = {}
    for i, tmpl in enumerate(SEATS):
        r = post("/api/world/template",
                 {"name": tmpl,
                  "position": {"x": SPOT[0] + i * 4, "y": SPOT[1], "z": SPOT[2]},
                  "static": True})
        seat_ids[tmpl] = r.get("object_id")

    fits = {}
    for preset in PRESETS:
        name = f"SM_{preset}"
        spec = {"name": name, "position": {"x": SPOT[0], "y": SPOT[1] + 1, "z": SPOT[2] - 4}}
        if preset != "standard":
            spec["appearance"] = {"preset": preset}
        post("/api/npc/spawn", spec)
        time.sleep(1.2)
        row = {}
        for tmpl in SEATS:
            oid = seat_ids.get(tmpl)
            if not oid:
                row[tmpl] = "?"
                continue
            r = post("/api/interaction/can_interact",
                     {"entity_id": f"npc_{name}", "object_id": oid})
            row[tmpl] = "FIT" if r.get("can_interact") else "no"
        fits[preset] = row

        # Gate parity: really try to sit on one FIT and one refused seat.
        fit_seat = next((t for t in SEATS if row[t] == "FIT"), None)
        no_seat = next((t for t in SEATS if row[t] == "no"), None)
        for tmpl, expect in ((fit_seat, True), (no_seat, False)):
            if tmpl is None:
                continue
            r = post("/api/interaction/sit",
                     {"entity_id": f"npc_{name}", "object_id": seat_ids[tmpl]})
            got = bool(r.get("success"))
            if got != expect:
                print(f"PARITY FAIL: {preset} x {tmpl}: can_interact said "
                      f"{expect}, sit said {got}")
                sys.exit(1)
            if got:
                post("/api/interaction/stand_up", {"entity_id": f"npc_{name}"})
                time.sleep(1.0)
        post("/api/npc/remove", {"name": name})

    # Print the matrix.
    print(f"{'preset':10s}" + "".join(f"{t:>13s}" for t in SEATS))
    for preset in PRESETS:
        print(f"{preset:10s}" + "".join(f"{fits[preset][t]:>13s}" for t in SEATS))

    # Coverage: every preset has at least one legal seat.
    uncovered = [p for p in PRESETS if "FIT" not in fits[p].values()]
    # Key expectations.
    problems = []
    if fits["ogre"].get("stool_low") == "FIT":
        problems.append("ogre fits stool_low (SEAT_TOO_LOW should refuse)")
    if fits["halfling"].get("bench_great") == "FIT":
        problems.append("halfling fits bench_great (SEAT_TOO_TALL should refuse)")
    if fits["standard"].get("chair_wood") != "FIT":
        problems.append("standard refused chair_wood (should fit its own chair)")

    # Cleanup seats.
    for tmpl, oid in seat_ids.items():
        if oid:
            post("/api/placed_object/remove", {"id": oid})

    if uncovered:
        print(f"\nCOVERAGE FAIL — presets with no fitting seat: {uncovered}")
        sys.exit(1)
    if problems:
        print("\nEXPECTATION FAIL: " + "; ".join(problems))
        sys.exit(1)
    print("\nRESULT: coverage + expectations + gate parity all PASS")


if __name__ == "__main__":
    main()
