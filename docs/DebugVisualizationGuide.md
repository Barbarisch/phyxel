# Debug Visualization Guide

## RaycastVisualizer — Architecture & Rules

`RaycastVisualizer` renders debug geometry (wireframe boxes, lines, crosses, raycast rays)
using a Vulkan line-list pipeline. It is toggled with **F5**.

### Two-Buffer Model

The visualizer has two distinct resources that must never be confused:

| Resource | Type | Owner | Purpose |
|---|---|---|---|
| `m_lines` / `m_previewBoxes` | CPU `std::vector` | Caller | Accumulate draw requests |
| `m_vertexBuffer` | Vulkan GPU buffer | `updateBuffers()` | Actual rendered data |
| `m_uploadedVertexCount` | `uint32_t` | `updateBuffers()` | How many vertices are in the GPU buffer |

`m_vertices` is a **staging buffer** — a temporary CPU vector used only inside
`generateDebugGeometry()` / `updateBuffers()` to build data before uploading.
**Nothing outside `updateBuffers()` should read `m_vertices.size()` to drive rendering.**

---

### Strict Phase Separation

The frame pipeline must follow this order:

```
update():
  1. addLine() / addPreviewBox() / setRaycastData()   — accumulate into CPU lists
  2. updateBuffers()                                   — ONE call builds + uploads to GPU
                                                         stores m_uploadedVertexCount

render():
  3. beginFrame()    — clears CPU lists for next frame (GPU buffer untouched)
  4. render()        — vkCmdDraw uses m_uploadedVertexCount (GPU count, not CPU count)
```

**Rule: `generateDebugGeometry()` is called exactly once per frame, inside `updateBuffers()`.**

---

### What Caused the Previous Bug

`generateDebugGeometry()` was being called from six different places:
`addLine()`, `setRaycastData()`, `addPreviewBox()`, `clearPreviewBoxes()`,
`clearLines()`, and `beginFrame()`.

`beginFrame()` was called inside `RenderCoordinator::render()` *after* `updateBuffers()`
had already run in `Application::update()`. It cleared `m_lines` and re-ran
`generateDebugGeometry()`, shrinking `m_vertices` back down to ~8 entries (just the
raycast ray). Then `render()` used `m_vertices.size()` as the draw count — drawing
only the first 8 vertices of a 264-vertex GPU buffer. Result: only the first 1–2
debug boxes were ever visible.

**The fix**: `beginFrame()` no longer calls `generateDebugGeometry()`. `render()` uses
`m_uploadedVertexCount` instead of `m_vertices.size()`.

---

### Rules for New Debug Drawing Code

1. **Accumulate, don't generate.** `addLine()` and similar functions just push to a list
   and set `m_dataChanged = true`. They never call `generateDebugGeometry()`.

2. **One upload per frame.** `updateBuffers()` is the only place that calls
   `generateDebugGeometry()` and writes to the GPU buffer.

3. **Never use the staging buffer as a draw parameter.** `m_vertices.size()` reflects
   the last time geometry was built, which may be stale. Use `m_uploadedVertexCount`.

4. **beginFrame() only clears CPU-side lists.** It must not regenerate geometry or
   touch the GPU buffer. The GPU buffer is valid until the next `updateBuffers()` call.

5. **clearData() resets `m_uploadedVertexCount` to 0.** This is how you signal to
   `render()` that there is nothing to draw.

---

### Segment Box Debug (F5 overlay)

When F5 is active (`raycastVisualizationEnabled`):

- The **player character mesh is hidden** (skipped in `RenderCoordinator::renderEntities()`)
- **8 wireframe boxes** are drawn by `AnimatedVoxelCharacter::drawSegmentBoxDebug()`,
  called from `update()` each frame via `addLine()`
- Each box also has a 3-axis cross marker at its center
- Colors: Magenta=head, Cyan=spine, Orange=arms (×4), Yellow=legs (×2), Red=collision hit
- The **green controller box** (movement boundary) is drawn by `Application.cpp`'s
  `drawCharacterHitbox()` — the actual physics box that controls how close to walls
  the character can stand

### Performance Notes

- `addLine()` and `addPreviewBox()` are O(1) — just a vector push
- `generateDebugGeometry()` is O(N lines + N preview boxes) — called once per frame
- `updateBuffers()` calls `vkDeviceWaitIdle` and recreates the GPU buffer every frame
  when data has changed. For a future optimization, pre-allocate a fixed-size buffer
  and only `memcpy` when the vertex count fits, recreating only when it needs to grow.
