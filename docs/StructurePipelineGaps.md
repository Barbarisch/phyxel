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
