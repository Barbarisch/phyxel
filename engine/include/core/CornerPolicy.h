#pragma once

// ============================================================================
// CornerPolicy — the ONE definition of the footprint-corner zone (Claims Ledger
// increment 4; docs/structure-generation/ClaimsLedger.md).
//
// The corner of a masonry footprint is structural (quoins / corner posts): no
// opening may be cut through it (KI-5a: blocked-slot shifting used to clamp
// windows straight onto the building corner). The zone matters at two pipeline
// stages: at PROGRAM time addTypologyWindows confines window PLACEMENT to the
// corner-safe band of each exterior edge (it consumes this policy); at REALIZE
// time the quoin pass dresses exactly the corner cubes and records CornerZone
// claims into the AssemblyPlan. CornerPolicyTest pins the realized CornerZone
// records INSIDE this policy's margin, so the two stages cannot drift apart —
// the rule lives here, not re-derived per consumer.
//
// kMarginCubes = 1 is REASONED from masonry corner integrity (the quoin /
// corner-post zone), not a sourced dimension — flagged, not silent
// (grounding discipline; see the KI-5a audit trail).
// ============================================================================

namespace Phyxel {
namespace Core {

struct CornerPolicy {
    static constexpr int kMarginCubes = 1;

    /// TRUE if an edge end at `coord` lands on the footprint boundary (0 or
    /// axisMax) — i.e. the edge reaches a footprint corner there.
    static bool endIsFootprintCorner(int coord, int axisMax) {
        return coord == 0 || coord == axisMax;
    }

    /// The corner-safe placement band [sLo, sHi) of an exterior edge [lo, hi)
    /// along a wall whose full axis is [0, axisMax]: each end that IS a
    /// footprint corner is inset by kMarginCubes; mid-wall room-boundary ends
    /// keep their full span.
    static void windowSafeBand(int lo, int hi, int axisMax, int& sLo, int& sHi) {
        sLo = endIsFootprintCorner(lo, axisMax) ? lo + kMarginCubes : lo;
        sHi = endIsFootprintCorner(hi, axisMax) ? hi - kMarginCubes : hi;
    }
};

} // namespace Core
} // namespace Phyxel
