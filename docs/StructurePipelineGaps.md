# Engine Gaps & Feature Needs (logged, not silently worked around)

Standing log of engine limitations hit during content/tool work. Each entry: what was needed,
what the engine did instead, the workaround used, and what a real fix looks like.

## 2026-07-05 — asset editor crashes after ~9 hot-reloads (exit 3, silent)

- **What happened:** driving the archetype visual survey via `POST /api/asset-editor/reload`
  (switching .voxel templates in a running `--asset-editor` instance), the engine process died
  with exit code 3 on the ~9th consecutive reload. The log shows the reload COMPLETED ("Asset
  Editor: scene ready") and then the process vanished — no error, no crash log. Smells like
  resource churn in the reload path (Vulkan buffer lifetime / double-free on the Nth scene
  teardown), possibly related to the vulkan transition crash noted in game-dev feedback round 3.
- **Workaround:** restart the asset editor process every ~4 reloads.
- **Real fix:** make `reload_asset` idempotent under churn — soak test: 50 consecutive reloads
  of mixed-size templates in one process; also `/api/asset-editor/reload`'s queueAndWait
  timeout (5s) is shorter than a large template's stamp time, so callers get "Request timed
  out waiting for game loop" for reloads that actually succeed — return an async job id or
  raise the timeout.

## 2026-07-07 — fill_region silently fails above the y=31→32 vertical-chunk seam

- **What happened:** during material-swap verification in the CharacterTestbed world,
  `fill_region` calls spanning y=30..33 placed exactly the y=30–31 half and reported the
  y=32–33 half as `failed: 8` — on all 10 fills, uniformly. `query_voxel` confirmed the
  "failed" cells were EMPTY AIR, not occupied: the failure is placement into the vertical
  chunk (cy=1) above the seam, not an occupancy skip. Same family as the vertical-chunk
  placement gap the 10-story-tower stress test surfaced for structures — apparently still
  present in the `fill_region` path (the target chunk may not be created/loaded on demand).
- **Workaround:** kept the test fixtures below y=32.
- **Real fix:** `fill_region` (and any direct placement route) must create/load the target
  chunk the way the structure placer now does, and the response should distinguish
  "occupied, skipped" from "placement failed" so seam bugs can't hide inside the failed
  count. Red test: fill a 2×4×2 box straddling y=31/32 in a fresh world, assert 16/16 placed.

## 2026-07-09 — build_settlement responses lost to the 5s queueAndWait timeout

- **What happened:** terrain-mode `POST /api/settlement/build` (era/tier village on Perlin
  hills) runs site analysis + per-parcel terracing + the MST path network on the game loop —
  well over the API's 5 s `queueAndWait` window. The caller gets `Request timed out waiting
  for game loop` while the settlement builds FINE seconds later; the response JSON (the
  program echo {era,tier,seed}, dropped_plots, below_tier_min, path stats) is simply lost, so
  callers must scrape phyxel.log for what the build reported. Flat-mode villages fit the
  window; terrain mode reliably does not. Same family as the asset-editor reload timeout
  (2026-07-05 entry). `generate_world` right after project load hits it too.
- **Workaround:** poll phyxel.log for `main_street terrain:` / `build_settlement:` lines.
- **Real fix:** route long-running composite commands (`build_settlement`, large
  `build_structure`) through the async job system (submit → job id → status returns the full
  response JSON), or raise/parameterize the queueAndWait window. The response payload matters
  here: it carries the determinism echo and the honest-degradation counts the discipline
  depends on.

## 2026-08-07 — schema:"v2" build_structure silently ignores `type` (typology defaults to hall_house)

- **Symptom:** `POST /api/structure/build {"schema":"v2","type":"tavern","footprint":[16,20],...}`
  builds a hall_house with zero tables — no error, no warning. The `type` → typology mapping
  (`tavern` → `tavern`) lives ONLY in the v1 compatibility conversion (the width/depth path in
  `StructureBuildService`); the direct v2 path reads `typology` and quietly falls back to the
  default when it is absent.
- Related: `"footprint"` must be a JSON ARRAY `[w,d]` — the object form `{"width","depth"}`
  realizes as a "realize failed: empty footprint" error.
- **Workaround:** always pass explicit `"typology"` (+ `"function"`) and the array footprint.
- **Real fix:** apply the same type→typology alias in the v2 path (or refuse a `type` that
  contradicts the resolved typology), and accept the object footprint shape or reject it with
  a message naming the array form.
