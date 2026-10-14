#ifndef WC_THETA_GLSL
#define WC_THETA_GLSL
// WaterCore 24.5 F2: the ONE ghost-fluid surface distance the GPU uses - the pressure matrix (wc_classify.comp) and the
// velocity update (wc_face_ops.comp) both include it, after declaring `f[]` and `solid[]`. It is
// WaterSolver::thetaToAir: the fraction of a cell from a liquid cell's centre to the free surface across face `dir`
// (0:-x 1:+x 2:-y 3:+y 4:-z 5:+z) - 0.5 at a side face; above, (f + s - 0.5) plus the thin layer in the cell above,
// one continuous expression (defect #28), clamped to [thetaMin, 1.5]. Until 2026-10-11 the velocity update kept the
// pre-#28 form (0.5 + f_above whenever there was water above, else capped at 0.5) while the matrix had this one: the
// two disagreed on where the surface was (WaterCore.md 24.5 F2), and both clamped at 0.1 (24.1).
float wcThetaToAir(int x, int y, int z, int dir) {
    if (dir == 3) {
        const float fa = inBounds(x, y + 1, z) ? min(1.0, f[cellIdx(x, y + 1, z)]) : 0.0;
        return clamp(f[cellIdx(x, y, z)] + solid[cellIdx(x, y, z)].x - 0.5 + fa, pc.thetaMin, 1.5);   // 20.5: a body raises the level it sits in
    }
    return 0.5;
}
// 26.9: the pressure the ghost cell holds across face `dir` - WaterSolver::surfaceGhost. Where the surface sits closer to
// the cell centre than the clamp (raw theta < thetaMin, the top cell just over the liquid threshold) the matrix and the
// velocity update use thetaMin; the ghost then holds the still-water pressure at that distance, -g (thetaMin - raw) h,
// so the cell's own pressure is g raw h - continuous as the cell crosses the threshold. With p_air = 0 the clamp moved
// the surface up by thetaMin h at every crossing; the cell beside it (air, just under the threshold) took a sideways push
// of 2 g thetaMin and a surface resting AT the threshold flickered in a checkerboard and never slept (26.9).
float wcSurfaceGhost(int x, int y, int z, int dir) {
    if (dir != 3 || pc.surfaceGhost == 0) return 0.0;
    const float fa = inBounds(x, y + 1, z) ? min(1.0, f[cellIdx(x, y + 1, z)]) : 0.0;
    const float raw = f[cellIdx(x, y, z)] + solid[cellIdx(x, y, z)].x - 0.5 + fa;
    return raw < pc.thetaMin ? -pc.gravity * pc.h * (pc.thetaMin - raw) : 0.0;
}
#endif
