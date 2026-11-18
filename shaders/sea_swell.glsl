// WaterCore §55.4 (W6.2a): the sea's swell - a GLSL port of SeaSwell.cpp's seaSwellComponents + seaSwellColumn, rule for rule (the
// swell's only definition is the CPU one; WaterCoreGpuParityTest.GpuSwellBoundaryMatchesTheCpuSwell pins this port to it within
// 1e-4 m and 1e-4 m/s). Four Gerstner components (wind + three spread directions; wavelengths l, 0.61 l, 0.33 l, 5 l; amplitudes
// a, 0.52 a, 0.28 a, 0.70 a; deep-water phase speed sqrt(g/k)); the column's height and DEPTH-AVERAGED velocity in water of depth d,
// depth-limited (the total height 2 sum a_i at most gamma 0.78 d), at most sqrt(g d); WITHOUT the Stokes return (stokesReturn false:
// the band is open to the sea, 55.4).
// The phase is reduced to one cycle before the sine (x / l_i - t / T_i, fract'ed): the GPU's sin loses accuracy far from 0, and
// the clock runs for hours.
#ifndef SEA_SWELL_GLSL
#define SEA_SWELL_GLSL

void seaSwellComponent(int i, float amp, float lambda, float wind, out vec2 dir, out float a, out float l) {
    if (i == 0)      { dir = vec2(cos(wind), sin(wind));               a = amp;         l = lambda; }
    else if (i == 1) { dir = vec2(cos(wind + 0.6), sin(wind + 0.6));   a = amp * 0.52;  l = lambda * 0.61; }
    else if (i == 2) { dir = vec2(cos(wind - 0.9), sin(wind - 0.9));   a = amp * 0.28;  l = lambda * 0.33; }
    else             { dir = vec2(cos(wind + 0.25), sin(wind + 0.25)); a = amp * 0.70;  l = lambda * 5.0; }
}

// height above the still level (m) and the depth-averaged velocity (m/s) at world (x, z), time t, still depth d
void seaSwellColumnG(float amp, float lambda, float wind, float x, float z, float t, float depth, out float height, out vec2 uAvg) {
    height = 0.0; uAvg = vec2(0.0);
    if (amp <= 0.0 || lambda <= 0.0) return;
    const float d = max(depth, 0.05);
    const float total = amp * (1.0 + 0.52 + 0.28 + 0.70);
    const float scale = min(1.0, 0.78 * d / max(2.0 * total, 1e-6));
    for (int i = 0; i < 4; ++i) {
        vec2 dir; float a0, l;
        seaSwellComponent(i, amp, lambda, wind, dir, a0, l);
        const float k = 6.28318530718 / l;
        const float cph = sqrt(9.81 / k);
        const float cyc = fract((dir.x * x + dir.y * z) / l - t * cph / l);   // k (dir.x - c t) / 2 pi, one cycle
        const float a = a0 * scale;
        const float eta = a * sin(6.28318530718 * cyc);
        height += eta;
        const float cd = sqrt(9.81 / k * tanh(k * d));
        uAvg += dir * (eta * cd / d);
        // no Stokes return (55.4): the band's sea is open - seaSwellColumn(..., stokesReturn = false)
    }
    const float froude = sqrt(9.81 * d), speed = length(uAvg);
    if (speed > froude) uAvg *= froude / speed;
}

#endif
