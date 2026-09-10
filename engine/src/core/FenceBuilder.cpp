#include "core/FenceBuilder.h"

#include <algorithm>

namespace Phyxel {
namespace Core {

FenceType fenceTypeFromString(const std::string& s) {
    if (s == "privacy") return FenceType::Privacy;
    if (s == "post_rail" || s == "postrail" || s == "post-rail") return FenceType::PostRail;
    return FenceType::Picket;
}
std::string fenceTypeToString(FenceType t) {
    switch (t) { case FenceType::Privacy: return "privacy"; case FenceType::PostRail: return "post_rail";
                 default: return "picket"; }
}
std::string fenceArchetype(FenceType t) {
    switch (t) { case FenceType::Privacy: return "fence_privacy"; case FenceType::PostRail: return "fence_post_rail";
                 default: return "fence_picket"; }
}

std::vector<int> fencePostPositions(int runLenMicro, int postSpacingMicro, bool endPosts) {
    std::vector<int> out;
    if (runLenMicro < 1) return out;
    if (postSpacingMicro < 1) postSpacingMicro = runLenMicro;
    const int span = runLenMicro - 1;
    if (span <= 0) { out.push_back(0); return out; }
    // `segs` gaps -> posts at i*span/segs land exactly on both ends and EVENLY between, so an interior
    // post never lands adjacent to an end post (the doubled corner). endPosts=false drops the two ends.
    // Cap segs so every gap stays >= ~1 cube (9 micro): on a very short run, ceil(span/spacing) would
    // otherwise place posts closer than a cube (a doubled corner on a tiny edge).
    int segs = (span + postSpacingMicro - 1) / postSpacingMicro;          // ceil(span/spacing)
    segs = std::min(segs, std::max(1, span / 9));                         // keep gaps >= ~1 cube
    segs = std::max(1, segs);
    for (int i = 0; i <= segs; ++i) {
        if (!endPosts && (i == 0 || i == segs)) continue;   // corner owned by the perpendicular run
        out.push_back(i * span / segs);
    }
    return out;
}

FenceProfile planFenceProfile(int runLenMicro, int heightMicro, int postSpacingMicro, int rails,
                              FenceType type, int thickMicro, bool endPosts) {
    FenceProfile p;
    if (runLenMicro < 1 || heightMicro < 1 || thickMicro < 1) return p;
    if (postSpacingMicro < 1) postSpacingMicro = runLenMicro;     // degenerate -> just end posts
    p.heightMicro = heightMicro;
    p.thickMicro = thickMicro;

    auto add = [&](int u, int y) { for (int w = 0; w < thickMicro; ++w) p.cells.push_back({u, y, w}); };
    auto column = [&](int u) { for (int y = 0; y < heightMicro; ++y) add(u, y); };  // a full-height post/slat

    // POSTS: evenly distributed (see fencePostPositions) so an interior post never lands adjacent to an
    // end/corner post; endPosts=false omits the end columns so runs meet at ONE shared corner post.
    for (int u : fencePostPositions(runLenMicro, postSpacingMicro, endPosts)) column(u);

    // RAILS: `rails` horizontal lines spanning the whole run (what an open fence's slats hang on).
    for (int r = 1; r <= rails; ++r) {
        const int ry = heightMicro * r / (rails + 1);
        for (int u = 0; u < runLenMicro; ++u) add(u, ry);
    }

    // INFILL by type: privacy = solid close boards; picket = spaced vertical slats (gaps); post-rail =
    // nothing (posts + rails only -> open).
    if (type == FenceType::Privacy) {
        for (int u = 0; u < runLenMicro; ++u) column(u);
    } else if (type == FenceType::Picket) {
        for (int u = 0; u < runLenMicro; u += 2) {                // slat / gap / slat ...
            // KI-5f corner ownership: a run that doesn't own the corner posts must
            // not drop slats on the corner columns either (the counting guard caught
            // picket end-slats double-writing the perpendicular run's corner post).
            if (!endPosts && (u == 0 || u == runLenMicro - 1)) continue;
            column(u);
        }
    }

    p.ok = true;
    return p;
}

std::vector<FenceRun> planParcelFenceRuns(int prX, int prZ, int prW, int prD) {
    // The four fence planes, at micro row 0 of their boundary cubes (matching the
    // legacy stamper's w=0 mapping): W/S on the parcel's outer faces, E/N one cube
    // in, 8 micro inside the outer corner. Every run spans EXACTLY plane-to-plane
    // (inclusive), so all four ends land on the corner intersections; N/S own the
    // corner posts, W/E omit theirs (endPosts=false) — one post per corner, and the
    // perpendicular rails/pickets reach it.
    const int xW = prX * 9, xE = (prX + prW - 1) * 9;
    const int zS = prZ * 9, zN = (prZ + prD - 1) * 9;
    std::vector<FenceRun> runs;
    runs.push_back({true,  zS, xW, xE + 1, true,  'S'});
    runs.push_back({true,  zN, xW, xE + 1, true,  'N'});
    runs.push_back({false, xW, zS, zN + 1, false, 'W'});
    runs.push_back({false, xE, zS, zN + 1, false, 'E'});
    return runs;
}

bool fenceRunPinchesNeighbour(const FenceRun& run, int prX, int prZ, int prW, int prD,
                              const std::vector<CubeRect>& neighbours, int minAlleyCells) {
    if (run.toMicro <= run.fromMicro || minAlleyCells <= 0) return false;
    auto divFloor = [](int a, int b) { return (a >= 0) ? a / b : -((-a + b - 1) / b); };
    // Inclusive cube span of the run along its axis.
    const int s0 = divFloor(run.fromMicro, 9), s1 = divFloor(run.toMicro - 1, 9);
    // N/E planes sit at micro row 0 of the parcel's LAST cube: 8 micro of that cube lie
    // between the fence and the parcel's outer edge and count as clear width.
    const bool innerPlane = (run.side == 'N' || run.side == 'E');
    for (const CubeRect& nb : neighbours) {
        const int n0 = run.alongX ? nb.x : nb.z;
        const int n1 = run.alongX ? nb.x + nb.w - 1 : nb.z + nb.d - 1;
        if (n1 < s0 || n0 > s1) continue;                      // no overlap along the run
        int gapCells;                                          // clear cells outside the plane
        switch (run.side) {
            case 'S': gapCells = prZ - (nb.z + nb.d); break;   // neighbour on the -z side
            case 'N': gapCells = nb.z - (prZ + prD); break;    // neighbour on the +z side
            case 'W': gapCells = prX - (nb.x + nb.w); break;   // neighbour on the -x side
            case 'E': gapCells = nb.x - (prX + prW); break;    // neighbour on the +x side
            default:  continue;
        }
        if (gapCells < 0) continue;                            // not outside this plane
        const int clearMicro = gapCells * 9 + (innerPlane ? 8 : 0);
        if (clearMicro < minAlleyCells * 9) return true;
    }
    return false;
}

bool trimFenceRunAtDroppedCorners(FenceRun& run, bool droppedW, bool droppedE,
                                  bool droppedS, bool droppedN, int cells) {
    const int trim = std::max(0, cells) * 9;
    bool trimmed = false;
    if (run.alongX) {                       // S/N planes: corners with the W (start) / E (end) planes
        if (droppedW) { run.fromMicro += trim; trimmed = true; }
        if (droppedE) { run.toMicro   -= trim; trimmed = true; }
    } else {                                // W/E planes: corners with the S (start) / N (end) planes
        if (droppedS) { run.fromMicro += trim; trimmed = true; }
        if (droppedN) { run.toMicro   -= trim; trimmed = true; }
    }
    if (trimmed) run.cornerPosts = true;   // the run's ends are now free ends: post them
    return run.toMicro - run.fromMicro >= 9;
}

bool fenceGateWindow(int runLenMicro, int gateWidthCubes, int& loMicro, int& hiMicro) {
    if (runLenMicro <= 0 || gateWidthCubes <= 0) return false;
    // runLenMicro = (cubes-1)*9 + 1, so ceil-div recovers the cube span; centre in CUBES.
    const int cubeSpan = (runLenMicro + 8) / 9;
    if (cubeSpan < gateWidthCubes) return false;
    const int gs = ((cubeSpan - gateWidthCubes) / 2) * 9;
    loMicro = gs;
    hiMicro = gs + gateWidthCubes * 9;
    return true;
}

bool fenceGateWindowAt(int runLenMicro, int gateWidthCubes, int preferredCentreMicro,
                       int& loMicro, int& hiMicro) {
    if (runLenMicro <= 0 || gateWidthCubes <= 0) return false;
    const int cubeSpan = (runLenMicro + 8) / 9;
    if (cubeSpan < gateWidthCubes) return false;
    // Cube-aligned like the legacy window, but the gate's start cube tracks the door: centre
    // the gate on preferredCentreMicro, then clamp the whole window inside the run's cube span.
    int startCube = (preferredCentreMicro - gateWidthCubes * 9 / 2 + 4) / 9;
    startCube = std::max(0, std::min(startCube, cubeSpan - gateWidthCubes));
    loMicro = startCube * 9;
    hiMicro = loMicro + gateWidthCubes * 9;
    return true;
}

}  // namespace Core
}  // namespace Phyxel
