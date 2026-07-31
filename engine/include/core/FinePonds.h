#pragma once

#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <vector>

namespace Phyxel {

// ── Fine-scale pond discovery (tangible-water Phase B) ────────────────────────────────────────
//
// The hydrology bake is 128 u/cell, so depressions smaller than a cell are never wet — genuinely
// SMALL ponds don't exist in generated worlds without this pass. Discovery runs a bounded local
// depression fill over a height window: Priority-Flood seeded from the window BORDER (the
// outside world is "open"), so each interior cell's fill level is the lowest spill over any path
// out. Wet components (fill > terrain) become ponds; any component that touches the window
// border is DISCARDED — that is the boundedness rule that makes per-window analysis
// order-independent and seam-free: an accepted pond's ENTIRE basin lies inside this one window,
// so every consumer that can see it computes it identically.
//
// The pond's water level is spill − kPondFreeboard: filled to just under its lowest overflow
// point, so the hydrated water CANNOT leak by construction — no shoreline snap needed.
struct FinePond {
    float      level = 0.0f;              // water surface (spill − kPondFreeboard)
    float      depth = 0.0f;              // spill − deepest terrain
    glm::ivec2 bboxMin{0}, bboxMax{0};    // inclusive, window-local cells
    glm::ivec2 deepest{0, 0};             // window-local deepest column (ownership anchor)
    std::vector<uint32_t> columns;        // window-local packed (x << 16 | z), sorted
};

inline constexpr float kPondFreeboard = 0.15f;
inline constexpr float kPondMinDepth  = 1.5f;   // shallower dips aren't worth water
inline constexpr int   kPondMinArea   = 4;      // columns
inline constexpr int   kPondMaxArea   = 200;    // columns (bigger = the bake's job)

// Pure. heightAt(x, z) over [0,w)×[0,h) → all accepted ponds in the window.
std::vector<FinePond> discoverFinePonds(const std::function<float(int, int)>& heightAt,
                                        int w, int h);

}  // namespace Phyxel
