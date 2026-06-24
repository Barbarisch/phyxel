#include "core/StairPlanner.h"

#include <algorithm>

namespace Phyxel {
namespace Core {

StairForm stairFormFromString(const std::string& s) {
    if (s == "straight") return StairForm::Straight;
    return StairForm::Switchback;   // default / unknown
}
std::string stairFormToString(StairForm f) {
    return f == StairForm::Straight ? "straight" : "switchback";
}

namespace {

constexpr int MIN_TREAD     = 2;   ///< micro (~0.22 m): shallowest usable tread depth
constexpr int COMFORT_RISER = 2;   ///< micro (~0.22 m): IRC-ish comfortable riser target

// Fit a flight rising H micro into runMicro of available run, keeping riser ≤ maxStep.
// Prefers the comfort riser, only steepening (more rise per tread, fewer treads) when
// run is tight. Returns false when even the steepest compliant flight won't fit.
bool fitFlight(int H, int runMicro, int maxStep, int& treads, int& T, int& R) {
    if (H <= 0) { treads = 0; T = 0; R = 0; return true; }
    if (runMicro < MIN_TREAD || maxStep < 1) return false;
    const int treadsMin = (H + maxStep - 1) / maxStep;   // fewest treads to keep riser ≤ maxStep
    const int treadsMax = runMicro / MIN_TREAD;          // most treads the run can hold
    if (treadsMax < treadsMin) return false;             // run too short for a walkable flight
    const int pref = (H + COMFORT_RISER - 1) / COMFORT_RISER;
    treads = std::max(treadsMin, std::min(pref, treadsMax));
    T = runMicro / treads;                               // ≥ MIN_TREAD by construction
    R = (H + treads - 1) / treads;                       // ≤ maxStep since treads ≥ treadsMin
    return true;
}

}  // namespace

StairPlan planStair(int wellW, int wellD, int riseMicro, StairForm form, int maxStepMicro) {
    StairPlan p;
    if (wellW < 1 || wellD < 1 || riseMicro <= 0) { p.error = "degenerate well/rise"; return p; }

    const int WM = wellW * 9, DM = wellD * 9;

    if (form == StairForm::Switchback) {
        if (wellW < 2) { p.error = "switchback needs well width ≥ 2 (two lanes)"; return p; }
        const int laneW = WM / 2;                     // lane A = [0,laneW), lane B = [laneW,WM)
        const int Ld    = std::min(9, DM / 3);        // mid-landing depth (≤ 1 cube)
        const int runM  = DM - Ld;                    // run available to each half-flight
        const int h1 = riseMicro / 2, h2 = riseMicro - h1;

        int t1, T1, R1;
        if (!fitFlight(h1, runM, maxStepMicro, t1, T1, R1)) {
            p.error = "lower flight does not fit walkably in well"; return p;
        }
        const int runUsed = t1 * T1;                  // align both flights + landing to this run
        int t2, T2, R2;
        if (!fitFlight(h2, runUsed, maxStepMicro, t2, T2, R2)) {
            p.error = "upper flight does not fit walkably in well"; return p;
        }

        // Flight 1 — lane A, running +z, rising 0 → h1 (each tread solid from base up).
        for (int i = 0; i < t1; ++i) {
            const int top = std::min(h1, (i + 1) * R1);
            p.solids.push_back({0, 0, i * T1, laneW, top, T1});
        }
        // Mid-landing — both lanes, at h1, just past the run.
        p.solids.push_back({0, 0, runUsed, WM, h1, Ld});
        // Flight 2 — lane B, running −z back from the landing, rising h1 → rise.
        for (int j = 0; j < t2; ++j) {
            const int top = std::min(riseMicro, h1 + (j + 1) * R2);
            const int z0  = std::max(0, runUsed - (j + 1) * T2);
            p.solids.push_back({laneW, 0, z0, WM - laneW, top, T2});
        }

        p.maxRiserMicro = std::max(R1, R2);
        p.topMicro = riseMicro;
        p.holeX = 0; p.holeZ = 0; p.holeW = WM; p.holeD = DM;   // the shaft opening
        p.ok = (p.maxRiserMicro <= maxStepMicro);
        return p;
    }

    // ---- Straight: one flight, full width, along the longer axis ----
    const bool runZ  = wellD >= wellW;
    const int  runM  = runZ ? DM : WM;
    const int  crossW = runZ ? WM : DM;
    int t, T, R;
    if (!fitFlight(riseMicro, runM, maxStepMicro, t, T, R)) {
        p.error = "straight flight does not fit walkably in well (run too short)";
        p.holeX = 0; p.holeZ = 0; p.holeW = WM; p.holeD = DM;
        return p;
    }
    for (int i = 0; i < t; ++i) {
        const int top = std::min(riseMicro, (i + 1) * R);
        if (runZ) p.solids.push_back({0,     0, i * T, crossW, top, T});       // tread runs along z
        else      p.solids.push_back({i * T, 0, 0,     T,      top, crossW});  // tread runs along x
    }
    p.maxRiserMicro = R;
    p.topMicro = riseMicro;
    p.holeX = 0; p.holeZ = 0; p.holeW = WM; p.holeD = DM;
    p.ok = (R <= maxStepMicro);
    return p;
}

}  // namespace Core
}  // namespace Phyxel
