#pragma once

#include "core/Chunk.h"
#include "core/water/WaterCore.h"

#include <map>
#include <vector>

// WaterCore Phase D slice D1 (docs/WaterCore.md 16.2): the PURE half of writing an active volume
// back into chunk spans and reading spans back into a volume. Everything here is a function of
// world-space column runs and per-chunk local spans; nothing touches a ChunkManager, so the
// chunk-seam equality test (14.2 test 1) runs on synthetic data.
//
// A world-space column holds a sorted list of runs [bottomY, topY). Chunks store CLIPS of those
// runs (Chunk::WaterSpanLocal, local Y, top == 32 means "continues above", bottom == 0 under a
// clip whose lower neighbour reaches 32 means "continues below"). A column's water is ONE object
// stored in as many vertical chunks as it crosses; the functions below assemble it, edit it in
// world space, and clip it back, so the two clips of a run can never disagree.
namespace Phyxel::Core::Water {

struct WorldSpan {
    int   x = 0, z = 0;          ///< world voxel column
    float bottomY = 0.0f;        ///< world Y
    float topY = 0.0f;           ///< world Y; valid iff topY > bottomY
};

/// Clip one world span into the vertical chunk `cy` (local coordinates). False when it misses.
bool clipSpanToChunk(const WorldSpan& s, int cy, Chunk::WaterSpanLocal& out);

/// Assemble a chunk column's world spans from its vertical chunks' local spans. `chunks` maps
/// cy -> that chunk's spans (resident chunks only; a missing cy simply contributes nothing).
/// Clips that meet at a chunk border (top == 32 below, bottom == 0 above) are joined into one run.
/// Output is sorted by (x, z, bottomY).
std::vector<WorldSpan> assembleColumnSpans(const std::map<int, const std::vector<Chunk::WaterSpanLocal>*>& chunks, int cx, int cz);

/// Replace the runs of column (x, z) that intersect [yLo, yHi) with `runs` (world Y), keeping the
/// parts of existing spans that lie outside the range. Pure; returns the merged, sorted spans of
/// that column only.
std::vector<WorldSpan> mergeColumnRuns(const std::vector<WorldSpan>& existing, int x, int z, float yLo, float yHi, const std::vector<ColumnRun>& runs);

/// Clip world spans (any columns of ONE chunk column cx, cz) into per-vertical-chunk local spans,
/// each list sorted to the storage contract ((x, z) ascending, then bottom). Chunks that receive
/// nothing are present with an empty list only if `cyRange` names them (so a drained chunk is
/// cleared), where cyRange = [cyLo, cyHi] inclusive.
void clipSpansToChunks(const std::vector<WorldSpan>& spans, int cx, int cz, int cyLo, int cyHi,
                       std::map<int, std::vector<Chunk::WaterSpanLocal>>& out);

/// World spans of a box's columns (from assembled chunk columns), clipped to [yLo, yHi), as runs
/// for seedGridFromRuns. Mass of a run = depth (1 m^2 columns).
void spansToColumnRuns(const std::vector<WorldSpan>& spans, float yLo, float yHi, std::vector<ColumnRuns>& out);

/// 58 item 1: the span writer's work, counted (a cost bound a test can hold) - the spans read, merged and clipped.
struct ChunkColumnWriteStats {
    long spansVisited = 0;    ///< existing spans looked at (assembled, filtered, merged against)
    int chunksChanged = 0;    ///< vertical chunks whose list `out` holds
};

/// The record's write into ONE chunk column (cx, cz): `have` = cy -> the resident chunks' spans (the box's range and those around
/// it), `cols` = the written columns' runs (world Y, inside [yLo, yHi)). Each written column's spans inside [yLo, yHi) are replaced
/// by its runs, the parts outside kept (mergeColumnRuns); every other column is left as it is. `out` receives the new list of every
/// chunk in `have` whose list changed (sorted to the storage contract). The live span writer (Application) is this function.
void writeRunsIntoChunkColumn(const std::map<int, const std::vector<Chunk::WaterSpanLocal>*>& have, int cx, int cz,
                              const std::vector<const ColumnRuns*>& cols, float yLo, float yHi,
                              std::map<int, std::vector<Chunk::WaterSpanLocal>>& out, ChunkColumnWriteStats* stats = nullptr);

}  // namespace Phyxel::Core::Water
