#pragma once

#include <cstdint>
#include <vector>

namespace Phyxel {

// ── WATER SPAN — water as chunk-keyed world data (docs/Water.md §2 layer 1) ──────────────────────
// One contiguous run of water in one column of a 32^3 chunk cell, CLIPPED to that chunk's vertical
// range, in chunk-LOCAL coordinates. A span crossing a vertical chunk border is stored in BOTH
// chunks, each holding its clip (top == 32.0 = continues above). `top` is float on purpose: the
// fractional surface fill round-trips without quantising (a lake at 148.9 stays 148.9).
// (Was Chunk::WaterSpanLocal; moved out when D4 deleted today's Chunk store. The span STORE lives
// on ChunkManager, keyed by chunk coordinate.)
struct WaterSpanLocal {
    uint8_t x = 0, z = 0;      // local column, 0..31
    float   bottom = 0.0f;     // local Y of the span's base (rests on terrain or chunk floor)
    float   top = 0.0f;        // local Y of the surface / clip ceiling; valid iff top > bottom
};

using WaterSpanList = std::vector<WaterSpanLocal>;

} // namespace Phyxel
