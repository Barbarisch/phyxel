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
#endif
