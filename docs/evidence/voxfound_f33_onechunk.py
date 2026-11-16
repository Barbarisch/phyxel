"""F3.3 live check: only the chunks an edit changed are saved (§3.5a point 7). Build the gate region (editable), clear
its saved rows, 6 brushes inside ONE chunk (255, 0, 3), save -> exactly 1 chunk saved; rebuild with load_saved -> that
chunk's hash = before the save, every other chunk = the generator's (and not marked edited).
Run after voxfound_f33_gate.py (same engine)."""
import json, urllib.request

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
REGION = {"type": "Perlin", "seed": 7, "flora": True, "hide_world": True, "editable": True,
          "region": {"from": [254, 0, 2], "to": [257, 1, 5]}}


def call(b):
    r = urllib.request.Request(API + P, data=json.dumps(b).encode(), method="POST", headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(r, timeout=900) as resp:
        return json.loads(resp.read().decode())


def hashes():
    return {tuple(c["chunk"]): (c["hash"], c["edited"]) for c in call({"store_hash": True})["chunks"]}


call(REGION)
call({"saved_clear": True})
G = hashes()   # the generator
base = (255 * 256, 0 * 256, 3 * 256)
for k in range(6):
    at = [base[0] + 30 + 20 * k, 150 + 3 * k, base[2] + 40 + 25 * k]
    r = call({"edit": {"material": "Stone" if k % 2 else "air", "brush": {"at": at, "size": (1, 2, 4, 8, 8, 4)[k]}}})
E = hashes()
s = call({"save": True})
r = call(dict(REGION, load_saved=True))
L = hashes()
target = (255, 0, 3)
out = {"edited_chunks": [list(c) for c, (h, e) in E.items() if e], "saved_chunks": s.get("saved_chunks"),
       "saved": [c["chunk"] for c in s.get("chunks", [])], "loaded": r.get("load_saved", {}).get("loaded_chunks"),
       "target_equal_after_load": L[target][0] == E[target][0], "target_differs_from_generator": E[target][0] != G[target][0],
       "others_equal_generator": sum(1 for c in G if c != target and L[c][0] == G[c][0]), "others": len(G) - 1,
       "others_marked_edited": sum(1 for c in G if c != target and L[c][1])}
print(json.dumps(out))
json.dump(out, open(__file__.replace(".py", ".json"), "w"), indent=1)
