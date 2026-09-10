#pragma once

// ============================================================================
// FenceBuilder — turn a fence RUN into a THIN, GROUNDED, TYPED fence profile (place_fence #22).
//
// A fence is NOT a 1 m cube wall. It is a thin line of posts + rails (+ pickets / close boards) at a
// grounded height. This builds the per-run micro-voxel profile from the dimension canon
// (resources/object_dimensions.json: fence_picket/privacy/post_rail — height, post_spacing, rails,
// all cited), so a picket comes out ~0.9 m tall, ~0.1 m thick, posts every ~1.8 m — measured against
// the canon by FenceProfileTest (a 1 m-thick or 1.6 m-tall "fence" FAILS).
//
// Units: micro = 1/9 m (1 cube = 9 micro). Local frame: u = ALONG the run, y = UP from the ground,
// w = ACROSS (thickness). The caller orients (u->edge axis, w->perpendicular) and seats y on terrain.
// ============================================================================

#include <string>
#include <vector>

namespace Phyxel {
namespace Core {

enum class FenceType { Picket, Privacy, PostRail };

FenceType   fenceTypeFromString(const std::string& s);   ///< unknown -> Picket
std::string fenceTypeToString(FenceType t);
std::string fenceArchetype(FenceType t);                 ///< canon id: "fence_picket" | "fence_privacy" | "fence_post_rail"

struct FenceMicro { int u = 0, y = 0, w = 0; };          ///< one micro-voxel in the run's local frame

struct FenceProfile {
    std::vector<FenceMicro> cells;   ///< the thin fence geometry along the run
    int heightMicro = 0;             ///< realized top (micro) — should match the canon height
    int thickMicro = 0;              ///< realized thickness (micro) across the run — THIN (a few micro), not 9
    bool ok = false;
};

/// Build a fence profile for a run of `runLenMicro` (along u), to `heightMicro` tall, with posts every
/// `postSpacingMicro`, `rails` horizontal rails, and a type-specific infill: Picket = spaced vertical
/// slats (gaps), Privacy = solid close boards, PostRail = posts + rails only (open). The fence is
/// `thickMicro` (a few micro) THICK, never a full cube. Deterministic. Caller supplies the canon-derived
/// dims (height/post_spacing/rails) so this stays pure + unit-testable against object_dimensions.json.
// Post column positions (micro offsets along a run) — EVENLY distributed so an interior post never
// lands adjacent to an end/corner post (the doubled, messy corner). endPosts=false omits the two end
// columns so the perpendicular run's post is the single shared corner. Pure + deterministic: this is
// the ground-truth the corner-cleanliness unit test asserts on (a picket fence's voxel density can't
// distinguish a post from a slat, so density-based world detection is unsound — test the plan instead).
std::vector<int> fencePostPositions(int runLenMicro, int postSpacingMicro, bool endPosts = true);

// endPosts: place a post at BOTH run ends (a run that owns its corners). Pass false for the runs whose
// corners are owned by the perpendicular run, so adjacent edges meet at ONE shared corner post instead
// of doubling it. Posts are EVENLY distributed (never colliding with the end posts).
FenceProfile planFenceProfile(int runLenMicro, int heightMicro, int postSpacingMicro, int rails,
                              FenceType type, int thickMicro = 1, bool endPosts = true);

/// One perimeter run of a parcel fence, in MICRO coords (KI-5f). Micro precision is
/// load-bearing: the fence planes sit at micro row 0 of their boundary cubes, so the
/// N/E planes are 8 micro INSIDE the parcel's outer corner — cube-granular runs
/// overshoot/undershoot the perpendicular plane and the fences pass each other.
struct FenceRun {
    bool alongX = true;      ///< run direction (true: along X at fixed z; false: along Z)
    int  fixedMicro = 0;     ///< the plane's world-micro row (perpendicular axis)
    int  fromMicro = 0;      ///< [fromMicro, toMicro) world-micro span along the run
    int  toMicro = 0;
    bool cornerPosts = true; ///< this run OWNS the corner posts (planFenceProfile endPosts)
    char side = 'S';         ///< S|N|W|E (gate matching)
};

/// The 4 perimeter runs of a rectangular parcel (cube rect in, micro runs out).
/// Corner contract (KI-5f, "fences don't come to a neat corner"): all four runs END
/// exactly ON the corner intersection points of the four fence planes; N/S own the
/// corner posts, W/E omit end posts (one post per corner) but their rails/pickets
/// reach it. The old composition used whole-cube spans + corner-excluded W/E: the
/// planes missed each other by up to 8 micro and rails stopped a cube short.
std::vector<FenceRun> planParcelFenceRuns(int prX, int prZ, int prW, int prD);

/// A neighbouring building footprint in settlement-local cubes (x, z, w, d).
struct CubeRect { int x = 0, z = 0, w = 0, d = 0; };

/// Ravenmere G-78 (2026-09-09): a parcel fence must not leave a sub-corridor residual
/// against a NEIGHBOURING building. The tavern's east wall face sat 1 cell from the next
/// parcel's picket line: a 1.0 m alley, 0.67 m at the corner quoin, and the only
/// street->shrine route threaded it with 0.11 m of lateral slack. True when `run` (one
/// of the four parcel planes) would leave fewer than `minAlleyCells` clear cells between
/// itself and any neighbour footprint on the OUTSIDE of that plane, over an overlapping
/// span along the run. The caller drops such a run (the side stays unfenced - the same
/// outcome the flush-building policy already produces). N/E planes sit 8 micro inside
/// their parcel's outer edge, which is counted toward the clear width.
bool fenceRunPinchesNeighbour(const FenceRun& run, int prX, int prZ, int prW, int prD,
                              const std::vector<CubeRect>& neighbours, int minAlleyCells);

/// When a plane is dropped for pinching, the two runs that met it at its corners still
/// plant their CORNER POSTS on the pinch line (regen #7: the west plane went, the south
/// plane's corner post at (-20,-5) still stood 0.67 m from the tavern's quoin). Shorten
/// a surviving run by `cells` at every end whose corner plane was dropped; a trimmed run
/// owns its end posts (the post moves inward with the run). Returns false when nothing
/// is left of the run.
bool trimFenceRunAtDroppedCorners(FenceRun& run, bool droppedW, bool droppedE,
                                  bool droppedS, bool droppedN, int cells);

/// The GATE WINDOW cut into a run: the half-open micro span [lo, hi) of `run` that is
/// left open so a character can walk in. Cube-ALIGNED and centred (a naive micro-space
/// formula drifts up to 4 micro off centre on odd spans -- an auditor-caught defect).
/// Returns false and leaves lo/hi untouched when this run carries no gate.
///
/// This exists because the arithmetic was duplicated between the fence stamper and the
/// walkability validator's composition. They were identical, but a validator that keeps
/// its own copy of the rule it validates stops being a check the moment either drifts.
bool fenceGateWindow(int runLenMicro, int gateWidthCubes, int& loMicro, int& hiMicro);

/// Gate window ALIGNED to the front door (CityForgePlan M3): like fenceGateWindow but the
/// gate's cube span is centred as close to `preferredCentreMicro` (run-local micro, e.g. the
/// building's front-wall midpoint projected onto the run) as the run allows — clamped so the
/// full gate stays inside the run. Cube-aligned like the legacy window. Returns false when the
/// run can't host the gate at all.
bool fenceGateWindowAt(int runLenMicro, int gateWidthCubes, int preferredCentreMicro,
                       int& loMicro, int& hiMicro);

}  // namespace Core
}  // namespace Phyxel
