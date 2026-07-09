#include "core/WorldGenerator.h"
#include "core/WorldRecipe.h"
#include "core/Chunk.h"
#include "core/Cube.h"
#include "utils/Logger.h"
#include <random>
#include <cmath>
#include <iostream>
#include <string>
#include <fstream>
#include <nlohmann/json.hpp>

namespace Phyxel {

using json = nlohmann::json;

// ─────────────────────────────────────────────────────────────────────────────
// Free-function terrain noise (docs/TerrainGenerationV2.md P0).
//
// The coarse-model source and the Layer-1 ridged detail both need noise that is a PURE
// function of (position, seed) — no WorldGenerator instance — so a copied generator (the
// streaming worker) samples identically. These carry the noise seed EXPLICITLY. The member
// WorldGenerator::noise3D/perlinNoise3D/etc. now delegate here, so behavior is unchanged and
// the seam-critical "do not mask the lattice index" property is preserved verbatim.
// ─────────────────────────────────────────────────────────────────────────────
namespace {

int tnHash(int x, int y, int z, uint32_t seed) {
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
    y = ((y >> 16) ^ y) * 0x45d9f3b;
    y = ((y >> 16) ^ y) * 0x45d9f3b;
    y = (y >> 16) ^ y;
    z = ((z >> 16) ^ z) * 0x45d9f3b;
    z = ((z >> 16) ^ z) * 0x45d9f3b;
    z = (z >> 16) ^ z;
    return (x ^ y ^ z) + static_cast<int>(seed);
}

float tnFade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
float tnLerp(float a, float b, float t) { return a + t * (b - a); }

float tnGrad(int hash, float x, float y, float z) {
    int h = hash & 15;
    float u = h < 8 ? x : y;
    float v = h < 4 ? y : h == 12 || h == 14 ? x : z;
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

// Gradient-noise lattice. NOTE: the lattice index is deliberately NOT masked with & 255 —
// masking creates a hard several-voxel cliff at every multiple of 256 (world coord 0, chunk
// edges). See the original comment in noise3D; preserved here verbatim.
float tnNoise3D(float x, float y, float z, uint32_t seed) {
    int xi = static_cast<int>(std::floor(x));
    int yi = static_cast<int>(std::floor(y));
    int zi = static_cast<int>(std::floor(z));
    float xf = x - std::floor(x);
    float yf = y - std::floor(y);
    float zf = z - std::floor(z);
    float u = tnFade(xf), v = tnFade(yf), w = tnFade(zf);
    int aaa = tnHash(xi, yi, zi, seed);
    int aba = tnHash(xi, yi + 1, zi, seed);
    int aab = tnHash(xi, yi, zi + 1, seed);
    int abb = tnHash(xi, yi + 1, zi + 1, seed);
    int baa = tnHash(xi + 1, yi, zi, seed);
    int bba = tnHash(xi + 1, yi + 1, zi, seed);
    int bab = tnHash(xi + 1, yi, zi + 1, seed);
    int bbb = tnHash(xi + 1, yi + 1, zi + 1, seed);
    float x1 = tnLerp(tnGrad(aaa, xf, yf, zf), tnGrad(baa, xf - 1, yf, zf), u);
    float x2 = tnLerp(tnGrad(aba, xf, yf - 1, zf), tnGrad(bba, xf - 1, yf - 1, zf), u);
    float y1 = tnLerp(x1, x2, v);
    x1 = tnLerp(tnGrad(aab, xf, yf, zf - 1), tnGrad(bab, xf - 1, yf, zf - 1), u);
    x2 = tnLerp(tnGrad(abb, xf, yf - 1, zf - 1), tnGrad(bbb, xf - 1, yf - 1, zf - 1), u);
    float y2 = tnLerp(x1, x2, v);
    return tnLerp(y1, y2, w);
}

float tnFbm(float x, float y, float z, int octaves, float persistence, float lacunarity, uint32_t seed) {
    float total = 0.0f, frequency = 1.0f, amplitude = 1.0f, maxValue = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        total += tnNoise3D(x * frequency, y * frequency, z * frequency, seed) * amplitude;
        maxValue += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }
    return total / maxValue;
}

// ── P0 grounded constants (docs/TerrainGenerationV2.md §P0 "Grounded values"). ──
constexpr float kSeaLevelY = 16.0f;   // engineering continuity (Flat stays Y=16); an arbitrary
                                      // unbounded origin, NOT a geographic figure (auditor-flagged).

// Ridged multifractal (Musgrave, musgrave.c): H=1.0, offset=1.0, gain=2.0; lacunarity=2.0 and
// octaves=6 from the libnoise/SharpNoise lineage (octaves is a tunable knob, not a fact).
constexpr float kRmH = 1.0f, kRmOffset = 1.0f, kRmGain = 2.0f, kRmLacunarity = 2.0f;
constexpr int   kRmOctaves = 6;
constexpr float kRmFreq = 0.0060f;    // base ridge wavelength (~167 world units) — steeper flanks
                                      // for rugged mountains; verified at runtime via TerrainReliefTest.

// Vertical scale (user decision 2026-07-09): COMPRESSED. Grandest peaks ~384 voxels above sea
// level; the continental base supplies up to +96, ridged detail supplies the rest.
constexpr float kContinentalMax = 96.0f;   // max low-frequency landmass rise above sea level (voxels)
constexpr float kContinentalMin = -40.0f;  // ocean/shelf floor below sea level (near-shore band; deep-ocean cap is P1/P2)
constexpr float kMountainAmp    = 288.0f;  // Mountains ridged relief amplitude (96 + 288 ≈ 384 peak)
constexpr float kHillAmp        = 80.0f;   // Perlin/Caves gentler rolling relief

// ── P1 material rules (docs/TerrainGenerationV2.md §P1; grounding-auditor 2026-07-09). ──
// Temperature field anchor: normalized [0,1] == mean-annual −5..+30 °C (Whittaker 1975 biome-
// diagram temperature axis; 35 °C span). DESIGN DECISION (stated) — the citable anchor that lets
// the snow line and alpine gate fall out of real physics instead of a hand-picked Y threshold.
constexpr float kTempSpanC   = 35.0f;
constexpr float kSnowTemp01  = 5.0f / kTempSpanC;   // 0 °C freezing == (0−(−5))/35 ≈ 0.143 normalized
// Environmental lapse rate 6.5 °C/km (ICAO/US Standard Atmosphere) × the P0 vertical compression
// (~15 m per terrain-voxel, near the Mont-Blanc end of the grounded 12.5–23 m range) = 0.0975
// °C/voxel; normalized by the 35 °C span → snow emerges from temperature+altitude, physically:
// poles snowy near sea level, temperate mid-mountains capped, tropics only at the tallest peaks.
constexpr float kLapse01PerVoxel = (6.5f / 1000.0f) * 15.0f / kTempSpanC;   // ≈ 0.00279 /voxel
// Angle of repose (loose soil/scree 30–40°, Wikipedia): 35° → tan ≈ 0.70 rise/run, applied in
// rendered voxel space (local high-frequency detail is 1:1, uncompressed — P0 steepFrac confirms).
// Above this the surface is exposed rock/scree, not soil. DESIGN pick (midpoint of a real range).
constexpr float kRockSlope   = 0.70f;
// NOTE: seabed sediment zonation (shallow sand → deeper gravel/mud) and coastal beaches are NOT
// here — both need real ocean depth / a coastline, which only exist once P2 lands the hydrology
// bake. At P1's compressed continental base the ocean floor bottoms out ~kContinentalMin below sea
// (≈40 voxels), so a 130 m shelf-break split would be dead code, and an altitude-only "beach" rule
// sands inland lowland (no shore to abut). Seabed is therefore uniform Sand for now; beaches + shelf
// sediment are deferred to P2 (docs/TerrainGenerationV2.md §P2).

float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
float smoothstep01(float edge0, float edge1, float x) {
    float t = clamp01((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}
// Multi-octave fbm clusters toward 0.5 (mushy middle). Push values out toward their extremes so
// climate/continentalness genuinely reach hot/cold, wet/dry, ocean/interior — otherwise the central
// full-range biome (Plains) wins almost everywhere. Applied to temperature, moisture, continentalness.
constexpr float kClimateContrast = 1.9f;
float expandContrast(float v) { return clamp01((v - 0.5f) * kClimateContrast + 0.5f); }

// Continental base elevation from continentalness [0,1] → world Y. A cubic "amplification
// spline": flat lowlands/shelf near sea level, exaggerated interior highlands. This is the
// Layer-0 low-frequency landmass; dramatic peaks come from the Layer-1 ridged detail on top.
float continentalBase(float cont) {
    float t = clamp01(cont);
    float shaped = t * t * (3.0f - 2.0f * t);   // smoothstep redistribution (flat low, steep high)
    return kSeaLevelY + kContinentalMin + (kContinentalMax - kContinentalMin) * shaped;
}

// Ridged multifractal (Musgrave). Returns ~[0, ~1.4]; peaks sharp (rough), valleys smooth,
// because each octave is weighted by the previous (detail only accumulates on high ground).
float tnRidgedMultifractal(float x, float z, uint32_t seed) {
    float freq = 1.0f, result = 0.0f, weight = 1.0f;
    float signal = kRmOffset - std::fabs(tnNoise3D(x, 0.0f, z, seed));
    signal *= signal;
    result = signal;
    for (int i = 1; i < kRmOctaves; ++i) {
        x *= kRmLacunarity;
        z *= kRmLacunarity;
        weight = clamp01(signal * kRmGain);
        signal = kRmOffset - std::fabs(tnNoise3D(x, 0.0f, z, seed));
        signal *= signal;
        signal *= weight;
        result += signal * std::pow(freq, -kRmH);
        freq *= kRmLacunarity;
    }
    return result;
}
constexpr float kRidgedNorm = 1.35f;  // ~max of tnRidgedMultifractal at these params → normalize to [0,1]

}  // namespace

WorldGenerator::WorldGenerator(GenerationType type, uint32_t seed)
    : generationType(type), seed(seed) {
    initDefaultBiomes();
    // Best-effort override from resources/biomes.json (CWD-relative, like materials.json).
    // Keeps the built-in defaults if the file is missing or invalid.
    loadBiomes("resources/biomes.json");
    rebuildCoarseModel();
}

// Build the Layer-0 coarse model. The source is PURE (captures only seed by value) so the
// worker's generator copy shares a valid, thread-safe model. cellSize = 32 (one sample per
// chunk): continentalness is very low frequency, so interpolating it per-chunk is effectively
// exact. Climate (temp/moisture) stays per-column in sampleColumn for P0; it migrates into the
// coarse model in P1 (biome overhaul), where biome-border changes are expected and tested.
void WorldGenerator::rebuildCoarseModel() {
    const uint32_t s = seed;
    const float contF = terrainParams.climateFrequency * 0.42f;  // continents larger than biomes
    m_coarse = std::make_shared<CoarseWorldModel>(
        [s, contF](float x, float z) {
            CoarseSample cs;
            auto to01 = [](float n) { return n < -1.0f ? 0.0f : (n > 1.0f ? 1.0f : (n + 1.0f) * 0.5f); };
            // Expand contrast so continents genuinely reach ocean (low) and mountainous-interior
            // (high) extremes instead of grey mush (see expandContrast).
            cs.continentalness = expandContrast(to01(tnFbm(x * contF, 300.0f, z * contF, 2, 0.5f, 2.0f, s)));
            cs.baseHeight = continentalBase(cs.continentalness);
            // temperature/moisture left at defaults for P0 (unused; sampleColumn computes them
            // per-column for biome selection). Filled in P1.
            return cs;
        },
        32.0f);
}

void WorldGenerator::generateChunk(Chunk& chunk, const glm::ivec3& chunkCoord) {
    // Non-height-based types keep their per-voxel paths (per-voxel random fill, building
    // grids, or a user-supplied function). Height-based types use the column-first pipeline.
    if (!isHeightBased()) {
        GenerationFunction generator;
        switch (generationType) {
            case GenerationType::Random:
                generator = [this](const glm::ivec3& c, const glm::ivec3& l) { return generateRandom(c, l); };
                break;
            case GenerationType::City:
                generator = [this](const glm::ivec3& c, const glm::ivec3& l) { return generateCity(c, l); };
                break;
            case GenerationType::Custom:
                generator = customGenerator;
                break;
            default:
                break;
        }
        if (!generator) {
            LOG_ERROR("WorldGenerator", "[WORLD_GENERATOR] No valid generator function!");
            return;
        }
        for (int x = 0; x < 32; ++x) {
            for (int z = 0; z < 32; ++z) {
                glm::ivec3 worldCol = chunkCoord * 32 + glm::ivec3(x, 0, z);
                float surfaceHeight = (generationType == GenerationType::City) ? 15.0f : 16.0f;
                for (int y = 0; y < 32; ++y) {
                    glm::ivec3 localPos(x, y, z);
                    if (generator(chunkCoord, localPos)) {
                        std::string material = getMaterialForPosition(
                            glm::ivec3(worldCol.x, chunkCoord.y * 32 + y, worldCol.z), surfaceHeight);
                        chunk.addCube(localPos, material);
                    }
                }
            }
        }
        return;
    }

    // Column-first height-based generation (Perlin / Flat / Mountains / Caves). The heightmap
    // + climate fields are sampled ONCE per (x,z) column, then the Y span is filled from them
    // (instead of recomputing noise per voxel). Biomes drive the surface material by climate.
    for (int x = 0; x < 32; ++x) {
        for (int z = 0; z < 32; ++z) {
            int wx = chunkCoord.x * 32 + x;
            int wz = chunkCoord.z * 32 + z;
            ColumnSample col = sampleColumn(wx, wz);

            for (int y = 0; y < 32; ++y) {
                int wy = chunkCoord.y * 32 + y;
                glm::ivec3 localPos(x, y, z);

                // Optional bedrock floor: empty below it, an indestructible layer at it.
                if (depthProfile.hasBedrock) {
                    if (wy < depthProfile.bedrockY) continue;
                    if (wy == depthProfile.bedrockY) { chunk.addCube(localPos, "Stone"); continue; }
                }

                if (wy > col.surfaceY) continue;  // above the surface: air (solid extends down)

                // Caves: carve 3D-noise pockets underground (kept from the legacy Caves path).
                if (generationType == GenerationType::Caves && wy < col.surfaceY - 2) {
                    float caveNoise = perlinNoise3D(wx * 0.05f, wy * 0.05f, wz * 0.05f, 3, 0.5f, 2.0f);
                    if (caveNoise > terrainParams.caveThreshold) continue;
                }

                chunk.addCube(localPos, materialForColumn(wy, col));
            }
        }
    }

    LOG_TRACE_FMT("WorldGenerator", "[WORLD_GENERATOR] Generated chunk (" << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z
              << ") column-first, biome-aware");
}

bool WorldGenerator::isHeightBased() const {
    switch (generationType) {
        case GenerationType::Perlin:
        case GenerationType::Flat:
        case GenerationType::Mountains:
        case GenerationType::Caves:
            return true;
        default:
            return false;  // Random, City, Custom keep per-voxel paths
    }
}

float WorldGenerator::surfaceVariationFor(int wx, int wz, float cont) {
    // Layer-1 mountain relief (docs/TerrainGenerationV2.md P0): ridged multifractal, domain-
    // warped so ridgelines bend, gated by a "mountainousness" mask from continentalness so
    // plains stay flat and only high continental interiors grow rough peaks. Returns voxels of
    // relief >= 0 (the continental base + sea level are added in sampleColumn). Flat = no relief.
    if (generationType == GenerationType::Flat) return 0.0f;

    // Domain warp the sample position (Quilez: fbm(p + fbm(p))) so ridges braid organically.
    const float warpF = 0.006f, warpAmp = 40.0f;
    const float wxw = wx + tnFbm(wx * warpF, 900.0f, wz * warpF, 2, 0.5f, 2.0f, seed) * warpAmp;
    const float wzw = wz + tnFbm(wx * warpF, 950.0f, wz * warpF, 2, 0.5f, 2.0f, seed) * warpAmp;

    float ridged = clamp01(tnRidgedMultifractal(wxw * kRmFreq, wzw * kRmFreq, seed) / kRidgedNorm);

    // Mountainousness mask (continentalness passed in by the caller): low continental land is
    // gentle, high continental interior is alpine. Mountains bias the whole map upward and rougher;
    // Perlin/Caves get gentler rolling hills.
    if (generationType == GenerationType::Mountains) {
        // Mask reaches full amplitude by mid-high continentalness (achievable after contrast
        // expansion), leaving low-continental coasts/valleys gentle. Peaks approach kMountainAmp.
        float mask = smoothstep01(0.20f, 0.70f, cont);
        return ridged * mask * kMountainAmp;
    }
    // Perlin / Caves: gentler, hills emerge above mid-continentalness.
    float mask = smoothstep01(0.40f, 0.85f, cont);
    return ridged * mask * kHillAmp;
}

WorldGenerator::ColumnSample WorldGenerator::sampleColumn(int wx, int wz) {
    ColumnSample col;
    // Low-frequency climate fields, decorrelated via distinct sample offsets, mapped to [0,1].
    // Continentalness is lower-frequency still (continents larger than biomes).
    auto to01 = [](float n) { float v = (n + 1.0f) * 0.5f; return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
    const float cf = terrainParams.climateFrequency;
    // Domain warp: offset the climate sample position by a separate noise so biome borders
    // are organic and wandering instead of straight Voronoi contours. 4 climate octaves add
    // fine detail so borders are ragged, not clean curves.
    const float warpAmp = 16.0f;
    const float warpF = cf * 2.0f;
    const float swx = wx + perlinNoise3D(wx * warpF, 500.0f, wz * warpF, 2, 0.5f, 2.0f) * warpAmp;
    const float swz = wz + perlinNoise3D(wx * warpF, 600.0f, wz * warpF, 2, 0.5f, 2.0f) * warpAmp;
    // Expand contrast so climate reaches its hot/cold and wet/dry extremes; without it the central
    // full-range biome (Plains) wins ~98% and extreme biomes (Desert/Tundra/Jungle) never appear.
    col.temperature     = expandContrast(to01(perlinNoise3D(swx * cf, 100.0f, swz * cf, 4, 0.5f, 2.0f)));
    col.moisture        = expandContrast(to01(perlinNoise3D(swx * cf, 200.0f, swz * cf, 4, 0.5f, 2.0f)));
    // Continentalness + continental base elevation now come from the Layer-0 coarse model
    // (docs/TerrainGenerationV2.md). It is very low frequency, so per-chunk interpolation is
    // effectively exact; this is the seam where Layer 1 reads the global landmass shape.
    const CoarseSample coarse = m_coarse->sample(static_cast<float>(wx), static_cast<float>(wz));
    col.continentalness = coarse.continentalness;

    // Biome selection + height blending: weight each biome by a Gaussian of the distance
    // from this column's climate to the biome's climate-cell CENTRE (a smooth Voronoi). The
    // dominant (nearest) biome supplies materials — hard material borders are fine — while the
    // height params are BLENDED across biomes so elevation transitions don't cliff.
    float totalW = 0.0f, blendScale = 0.0f, blendOffset = 0.0f, bestW = -1.0f;
    int best = 0;
    constexpr float kSigma2 = 2.0f * 0.15f * 0.15f;  // blend width in climate space
    for (size_t i = 0; i < m_biomes.size(); ++i) {
        const Biome& b = m_biomes[i];
        float ct = (b.tempMin + b.tempMax) * 0.5f;
        float cm = (b.moistMin + b.moistMax) * 0.5f;
        float cc = (b.contMin + b.contMax) * 0.5f;   // continentalness cell centre
        float dt = col.temperature - ct, dm = col.moisture - cm, dc = col.continentalness - cc;
        // Continentalness is a 3rd Voronoi axis. Biomes that leave it full-range share centre 0.5,
        // so dc is identical across them and cancels — temp+moisture alone decide (no regression).
        // A biome with a narrow continentalness range pulls its centre off 0.5 and genuinely gates
        // on ocean-distance/elevation (the hook for P2 ocean/coast biomes).
        float w = std::exp(-(dt * dt + dm * dm + dc * dc) / kSigma2);
        totalW += w;
        blendScale  += w * b.heightScale;
        blendOffset += w * b.heightOffset;
        if (w > bestW) { bestW = w; best = static_cast<int>(i); }
    }
    col.biomeIndex = best;
    if (totalW > 0.0f) { blendScale /= totalW; blendOffset /= totalW; }
    else { blendScale = 1.0f; blendOffset = 0.0f; }

    // Resolve the surface material, applying per-column scatter in patches (e.g. a forest
    // floor that's mostly dirt with grass patches). Patch-frequency noise so it clumps.
    const Biome& sel = m_biomes.empty() ? Biome{} : m_biomes[best];
    col.surfaceMat = sel.surfaceMaterial;
    if (!sel.surfaceAlt.empty() && sel.surfaceAltChance > 0.0f) {
        float s = to01(perlinNoise3D(wx * 0.15f, 700.0f, wz * 0.15f, 2, 0.5f, 2.0f));
        if (s < sel.surfaceAltChance) col.surfaceMat = sel.surfaceAlt;
    }

    if (generationType == GenerationType::Flat) {
        col.surfaceY = static_cast<int>(kSeaLevelY);  // Flat stays flat; biome affects material only
    } else {
        // surfaceY = Layer-0 continental base (sea level + landmass rise/ocean carve)
        //          + Layer-1 ridged mountain relief, scaled by the biome's height extremeness.
        // The old ±9 continental cap is gone — the base now spans kContinentalMin..kContinentalMax
        // and peaks reach ~kSeaLevelY + kContinentalMax + kMountainAmp (~384 above sea level).
        float relief = surfaceVariationFor(wx, wz, coarse.continentalness);
        col.surfaceY = static_cast<int>(std::floor(coarse.baseHeight + relief * blendScale + blendOffset));

        // P1 slope + altitude/temperature material overrides (docs/TerrainGenerationV2.md §P1).
        // These layer physical surfacing ON TOP of the biome material: a sand seabed below sea
        // level, exposed rock past the angle of repose, and a lapse-rate snow line. Moderate,
        // gently-sloped land keeps its biome surface. (Flat is exempt — it stays a clean biome map.)
        const int altitude = col.surfaceY - static_cast<int>(kSeaLevelY);
        // Local slope (rise/run) via central difference over ±1 column. Reuse the centre column's
        // biome blend (climate varies far slower than the ±1 step) instead of re-running biome
        // selection per neighbor — an approximation good to well under a voxel at this scale.
        auto nbSurfaceY = [&](int nx, int nz) {
            const CoarseSample c = m_coarse->sample(static_cast<float>(nx), static_cast<float>(nz));
            return c.baseHeight + surfaceVariationFor(nx, nz, c.continentalness) * blendScale + blendOffset;
        };
        const float dhx = (nbSurfaceY(wx + 1, wz) - nbSurfaceY(wx - 1, wz)) * 0.5f;
        const float dhz = (nbSurfaceY(wx, wz + 1) - nbSurfaceY(wx, wz - 1)) * 0.5f;
        const float slope = std::sqrt(dhx * dhx + dhz * dhz);
        const float effTemp = col.temperature - altitude * kLapse01PerVoxel;  // colder with altitude

        if (col.surfaceY < static_cast<int>(kSeaLevelY)) {
            col.surfaceMat = "Sand";    // ocean floor / seabed (water itself arrives in P2)
        } else if (slope > kRockSlope) {
            col.surfaceMat = "Stone";   // too steep for soil to hold → exposed rock / scree
        } else if (effTemp < kSnowTemp01) {
            col.surfaceMat = "Ice";     // permanent snow (Ice = closest palette material; see follow-up)
        }
        // else: keep the biome surface material set above (moderate, gently-sloped land).
    }
    return col;
}

std::string WorldGenerator::materialForColumn(int worldY, const ColumnSample& col) const {
    if (m_biomes.empty()) return "Stone";
    const Biome& b = m_biomes[col.biomeIndex];
    const int depth = col.surfaceY - worldY;  // 0 at the surface
    if (depth <= 0) return col.surfaceMat;  // per-column (may be a scatter patch)
    if (depth < 4)  return b.subsurfaceMaterial;
    return b.deepMaterial;
}

bool WorldGenerator::floraCellLayer(int cx, int cz, int layerIdx, FloraPlacement& out) {
    if (m_biomes.empty()) return false;
    auto hashu = [](int a, int b, uint32_t salt) -> uint32_t {
        uint32_t h = static_cast<uint32_t>(a) * 374761393u + static_cast<uint32_t>(b) * 668265263u
                   + salt * 2246822519u;
        h = (h ^ (h >> 13)) * 1274126177u; h ^= h >> 16; return h;
    };
    auto h01 = [&](int a, int b, uint32_t s) { return (hashu(a, b, s) & 0xFFFFFFu) / static_cast<float>(0x1000000); };

    // Per-layer salt so each layer jitters + prioritizes INDEPENDENTLY — a giant and an understory
    // plant can each win their own layer at neighboring cells (sparse giants over dense floor).
    const uint32_t lsalt = static_cast<uint32_t>(layerIdx) * 0x9E3779B1u;

    // Jittered site within the cell (so plants aren't on a visible lattice).
    const int jx = cx * kFloraGrid + static_cast<int>(hashu(cx, cz, seed ^ (0xA1u + lsalt)) % kFloraGrid);
    const int jz = cz * kFloraGrid + static_cast<int>(hashu(cx, cz, seed ^ (0xB2u + lsalt)) % kFloraGrid);

    ColumnSample col = sampleColumn(jx, jz);
    const Biome& biome = m_biomes[col.biomeIndex];

    // Physical surface gate (P1): flora follows the surfaced material, not just the biome, so trees
    // don't grow on the seabed, bare-rock cliffs, or snow above the treeline. A snow (Ice) surface
    // that came from the lapse-rate override on a NON-snow biome blocks flora; a biome whose OWN
    // surface is Ice (Snow biome = boreal conifers) keeps its trees. sampleColumn already applied
    // the override to col.surfaceMat. (docs/TerrainGenerationV2.md §P1)
    if (col.surfaceY < static_cast<int>(kSeaLevelY)) return false;              // seabed / underwater
    if (col.surfaceMat == "Stone") return false;                                // cliff (slope override; no biome surfaces Stone)
    if (col.surfaceMat == "Ice" && biome.surfaceMaterial != "Ice") return false; // snow-capped non-snow biome

    // Resolve this layer's config: layer 0 = the biome's flat flora fields, 1+ = extraFloraLayers.
    float density; int spacingRaw; std::string mode; float fullness;
    const std::vector<std::pair<std::string, int>>* items;
    if (layerIdx == 0) {
        density = biome.floraDensity; spacingRaw = biome.floraSpacing;
        mode = biome.floraMode; fullness = biome.floraFullness; items = &biome.flora;
    } else {
        const int i = layerIdx - 1;
        if (i >= static_cast<int>(biome.extraFloraLayers.size())) return false;
        const FloraLayer& L = biome.extraFloraLayers[i];
        density = L.density; spacingRaw = L.spacing; mode = L.mode; fullness = L.fullness; items = &L.items;
    }
    if (items->empty() || density <= 0.0f) return false;

    // Local-maximum (Poisson-disk approximation) test: this cell wins only if its priority hash
    // beats every cell within this LAYER's spacing radius. Pure function of cell coords + seed +
    // layer → order-independent, so a streamed chunk and a whole-region pass place identical plants.
    const int spacing = std::max(2, spacingRaw);
    const int R = (spacing + kFloraGrid - 1) / kFloraGrid;
    const uint32_t psalt = (seed ^ 0x9E3779B9u) + lsalt;
    const uint32_t p = hashu(cx, cz, psalt);
    for (int nz = cz - R; nz <= cz + R; ++nz)
        for (int nx = cx - R; nx <= cx + R; ++nx) {
            if (nx == cx && nz == cz) continue;
            const uint32_t q = hashu(nx, nz, psalt);
            if (q > p || (q == p && (nz < cz || (nz == cz && nx < cx)))) return false;  // neighbor wins
        }

    // Density thinning of the spacing-separated winners.
    if (h01(jx, jz, seed ^ (0xC3u + lsalt)) >= density) return false;

    // Weighted template pick.
    int total = 0;
    for (const auto& f : *items) total += f.second;
    int pick = static_cast<int>(h01(jx, jz, seed ^ (0xD4u + lsalt)) * total);
    const std::string* chosen = &items->front().first;
    for (const auto& f : *items) { pick -= f.second; if (pick < 0) { chosen = &f.first; break; } }

    out = FloraPlacement{*chosen, jx, col.surfaceY, jz};
    out.procedural = (mode == "procedural");
    out.fullness = fullness;
    return true;
}

std::vector<WorldGenerator::FloraPlacement>
WorldGenerator::planFlora(int colMinX, int colMinZ, int colMaxX, int colMaxZ, int edgeInset) {
    std::vector<FloraPlacement> out;
    if (m_biomes.empty() || !isHeightBased()) return out;

    const int x0 = colMinX + edgeInset, x1 = colMaxX - edgeInset;
    const int z0 = colMinZ + edgeInset, z1 = colMaxZ - edgeInset;
    if (x1 < x0 || z1 < z0) return out;

    // Max flora layers across all biomes (layer 0 = flat fields; 1+ = extraFloraLayers).
    int maxLayers = 1;
    for (const auto& b : m_biomes)
        maxLayers = std::max(maxLayers, 1 + static_cast<int>(b.extraFloraLayers.size()));

    auto floordiv = [](int a, int b) { return (a >= 0) ? a / b : -((-a + b - 1) / b); };
    for (int cz = floordiv(z0, kFloraGrid); cz <= floordiv(z1, kFloraGrid); ++cz) {
        for (int cx = floordiv(x0, kFloraGrid); cx <= floordiv(x1, kFloraGrid); ++cx) {
            for (int layer = 0; layer < maxLayers; ++layer) {
                FloraPlacement p;
                if (floraCellLayer(cx, cz, layer, p) && p.worldX >= x0 && p.worldX <= x1 &&
                    p.worldZ >= z0 && p.worldZ <= z1)
                    out.push_back(std::move(p));
            }
        }
    }
    LOG_DEBUG_FMT("WorldGenerator", "planFlora: " << out.size() << " plants over ["
                  << x0 << ".." << x1 << "]x[" << z0 << ".." << z1 << "]");
    return out;
}

void WorldGenerator::initDefaultBiomes() {
    // Positional aggregate init — field order MUST match the Biome struct:
    // name, surface, subsurface, deep, tempMin, tempMax, moistMin, moistMax, contMin, contMax,
    // heightScale, heightOffset, surfaceAlt, surfaceAltChance. Selection is nearest climate-cell
    // CENTRE (temp+moisture+continentalness); height params blend smoothly across biomes. These
    // defaults use the full continentalness range (0..1) so they're chosen on temp+moisture alone.
    m_biomes = {
        {"Snow",    "Ice",        "Stone",     "Stone", 0.0f, 0.3f, 0.0f, 1.0f,  0.0f, 1.0f, 1.3f,  6.0f, "",      0.0f},
        {"Desert",  "Sand",       "Sandstone", "Stone", 0.6f, 1.0f, 0.0f, 0.35f, 0.0f, 1.0f, 0.5f, -2.0f, "",      0.0f},
        {"Savanna", "GrassSavanna","Dirt",     "Stone", 0.7f, 1.0f, 0.35f, 0.6f, 0.0f, 1.0f, 0.7f,  0.0f, "Dirt",  0.3f},
        {"Forest",  "GrassForest","Dirt",      "Stone", 0.3f, 0.7f, 0.6f, 1.0f,  0.0f, 1.0f, 1.0f,  1.0f, "Dirt",  0.6f},
        {"Plains",  "Grass",      "Dirt",      "Stone", 0.0f, 1.0f, 0.0f, 1.0f,  0.0f, 1.0f, 0.6f,  0.0f, "",      0.0f},
    };
}

WorldRecipe WorldGenerator::makeRecipe() const {
    WorldRecipe r;
    r.seed = seed;
    r.climateFrequency = terrainParams.climateFrequency;
    for (const auto& b : m_biomes) {
        WorldRecipe::BiomeTune bt;
        bt.name = b.name;
        bt.heightScale = b.heightScale;
        bt.floraMode = b.floraMode;
        bt.floraFullness = b.floraFullness;
        bt.floraDensity = b.floraDensity;
        bt.floraSpacing = b.floraSpacing;
        for (const auto& f : b.flora) bt.flora.push_back({f.first, f.second});
        for (const auto& L : b.extraFloraLayers) {
            WorldRecipe::FloraLayerTune lt;
            lt.density = L.density; lt.spacing = L.spacing; lt.mode = L.mode; lt.fullness = L.fullness;
            for (const auto& f : L.items) lt.items.push_back({f.first, f.second});
            bt.extraLayers.push_back(std::move(lt));
        }
        r.biomes.push_back(std::move(bt));
    }
    return r;
}

void WorldGenerator::applyRecipe(const WorldRecipe& recipe) {
    terrainParams.climateFrequency = recipe.climateFrequency;
    // Override per-biome tuning by name; biome category fields (materials, climate) untouched.
    for (const auto& bt : recipe.biomes) {
        for (auto& b : m_biomes) {
            if (b.name != bt.name) continue;
            b.heightScale = bt.heightScale;
            b.floraMode = bt.floraMode;
            b.floraFullness = bt.floraFullness;
            b.floraDensity = bt.floraDensity;
            b.floraSpacing = bt.floraSpacing;
            if (!bt.flora.empty()) {
                b.flora.clear();
                for (const auto& f : bt.flora) b.flora.emplace_back(f.templateName, f.weight);
            }
            b.extraFloraLayers.clear();
            for (const auto& lt : bt.extraLayers) {
                FloraLayer L;
                L.density = lt.density; L.spacing = lt.spacing; L.mode = lt.mode; L.fullness = lt.fullness;
                for (const auto& f : lt.items) L.items.emplace_back(f.templateName, f.weight);
                if (!L.items.empty()) b.extraFloraLayers.push_back(std::move(L));
            }
            break;
        }
    }
    // climateFrequency changed → the coarse model's continentalness frequency changed. Rebuild
    // it now so the worker snapshot (taken after applyRecipe) copies an up-to-date Layer 0.
    rebuildCoarseModel();
    LOG_INFO_FMT("WorldGenerator", "Applied world recipe (climateFreq=" << recipe.climateFrequency
                 << ", " << recipe.biomes.size() << " biome tunings)");
}

bool WorldGenerator::loadBiomes(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;
    json root;
    try {
        file >> root;
    } catch (const std::exception& e) {
        LOG_WARN_FMT("WorldGenerator", "biomes.json parse error in " << path << ": " << e.what() << " (keeping defaults)");
        return false;
    }
    if (!root.contains("biomes") || !root["biomes"].is_array()) return false;

    std::vector<Biome> loaded;
    for (const auto& b : root["biomes"]) {
        Biome biome;
        biome.name = b.value("name", "Biome");
        biome.surfaceMaterial = b.value("surface", "Grass");
        biome.subsurfaceMaterial = b.value("subsurface", "Dirt");
        biome.deepMaterial = b.value("deep", "Stone");
        if (b.contains("temp") && b["temp"].is_array() && b["temp"].size() == 2) {
            biome.tempMin = b["temp"][0].get<float>();
            biome.tempMax = b["temp"][1].get<float>();
        }
        if (b.contains("moisture") && b["moisture"].is_array() && b["moisture"].size() == 2) {
            biome.moistMin = b["moisture"][0].get<float>();
            biome.moistMax = b["moisture"][1].get<float>();
        }
        if (b.contains("continentalness") && b["continentalness"].is_array() && b["continentalness"].size() == 2) {
            biome.contMin = b["continentalness"][0].get<float>();
            biome.contMax = b["continentalness"][1].get<float>();
        }
        biome.heightScale = b.value("heightScale", 1.0f);
        biome.heightOffset = b.value("heightOffset", 0.0f);
        biome.surfaceAlt = b.value("surfaceAlt", "");
        biome.surfaceAltChance = b.value("surfaceAltChance", 0.0f);
        if (b.contains("flora") && b["flora"].is_object()) {
            const auto& f = b["flora"];
            biome.floraDensity = f.value("density", 0.0f);
            biome.floraSpacing = f.value("spacing", 6);
            biome.floraMode = f.value("mode", std::string("pool"));
            biome.floraFullness = f.value("fullness", 0.85f);
            if (f.contains("items") && f["items"].is_array()) {
                for (const auto& it : f["items"]) {
                    std::string tmpl = it.value("template", "");
                    int weight = it.value("weight", 1);
                    if (!tmpl.empty() && weight > 0) biome.flora.emplace_back(std::move(tmpl), weight);
                }
            }
        }
        // Optional additional flora bands, each with its own spacing/density (sparse giants over a
        // dense understory). Backward-compatible: absent = single-layer biome.
        if (b.contains("floraLayers") && b["floraLayers"].is_array()) {
            for (const auto& lj : b["floraLayers"]) {
                FloraLayer L;
                L.density = lj.value("density", 0.0f);
                L.spacing = lj.value("spacing", 6);
                L.mode = lj.value("mode", std::string("pool"));
                L.fullness = lj.value("fullness", 0.85f);
                if (lj.contains("items") && lj["items"].is_array())
                    for (const auto& it : lj["items"]) {
                        std::string tmpl = it.value("template", "");
                        int weight = it.value("weight", 1);
                        if (!tmpl.empty() && weight > 0) L.items.emplace_back(std::move(tmpl), weight);
                    }
                if (!L.items.empty()) biome.extraFloraLayers.push_back(std::move(L));
            }
        }
        loaded.push_back(std::move(biome));
    }
    if (loaded.empty()) return false;
    m_biomes = std::move(loaded);
    LOG_INFO_FMT("WorldGenerator", "Loaded " << m_biomes.size() << " biomes from " << path);
    return true;
}

bool WorldGenerator::generateRandom(const glm::ivec3& chunkCoord, const glm::ivec3& localPos) {
    // Use chunk coordinate and local position to create deterministic randomness
    std::mt19937 gen(seed + hash(chunkCoord.x, chunkCoord.y, chunkCoord.z) + hash(localPos.x, localPos.y, localPos.z));
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    
    // 70% chance of having a cube
    if (dist(gen) > 0.3f) {
        return true;
    }
    return false;
}

bool WorldGenerator::generatePerlin(const glm::ivec3& chunkCoord, const glm::ivec3& localPos) {
    // Convert to world coordinates
    glm::vec3 worldPos = glm::vec3(chunkCoord * 32 + localPos);
    
    // Generate height map using Perlin noise
    float height = perlinNoise3D(worldPos.x * terrainParams.frequency, 
                                 0.0f, 
                                 worldPos.z * terrainParams.frequency,
                                 terrainParams.octaves,
                                 terrainParams.persistence,
                                 terrainParams.lacunarity) * terrainParams.heightScale;
    
    // Add some base level
    height += 16.0f;
    
    // Create cube if current position is below height
    if (worldPos.y <= height) {
        return true;
    }
    
    return false;
}

bool WorldGenerator::generateFlat(const glm::ivec3& chunkCoord, const glm::ivec3& localPos) {
    glm::vec3 worldPos = glm::vec3(chunkCoord * 32 + localPos);
    
    // Flat world at Y=16
    if (worldPos.y <= 16.0f) {
        return true;
    }
    
    return false;
}

bool WorldGenerator::generateMountains(const glm::ivec3& chunkCoord, const glm::ivec3& localPos) {
    glm::vec3 worldPos = glm::vec3(chunkCoord * 32 + localPos);
    
    // Use multiple noise octaves for mountainous terrain
    float height = perlinNoise3D(worldPos.x * 0.01f, 0.0f, worldPos.z * 0.01f, 6, 0.7f, 2.0f) * 40.0f;
    height += perlinNoise3D(worldPos.x * 0.03f, 0.0f, worldPos.z * 0.03f, 4, 0.5f, 2.0f) * 20.0f;
    height += 20.0f; // Base level
    
    if (worldPos.y <= height) {
        return true;
    }
    
    return false;
}

bool WorldGenerator::generateCaves(const glm::ivec3& chunkCoord, const glm::ivec3& localPos) {
    glm::vec3 worldPos = glm::vec3(chunkCoord * 32 + localPos);
    
    // Base terrain
    float height = perlinNoise3D(worldPos.x * terrainParams.frequency, 
                                 0.0f, 
                                 worldPos.z * terrainParams.frequency,
                                 terrainParams.octaves,
                                 terrainParams.persistence,
                                 terrainParams.lacunarity) * terrainParams.heightScale + 16.0f;
    
    bool isGround = worldPos.y <= height;
    
    if (isGround && worldPos.y < height - 2.0f) { // Only create caves underground
        // 3D cave noise
        float caveNoise = perlinNoise3D(worldPos.x * 0.05f, worldPos.y * 0.05f, worldPos.z * 0.05f, 3, 0.5f, 2.0f);
        
        // Create cave if noise is above threshold
        if (caveNoise > terrainParams.caveThreshold) {
            return false; // Empty space (cave)
        }
    }
    
    if (isGround) {
        return true;
    }
    
    return false;
}

bool WorldGenerator::generateCity(const glm::ivec3& chunkCoord, const glm::ivec3& localPos) {
    glm::vec3 worldPos = glm::vec3(chunkCoord * 32 + localPos);
    
    // Flat ground first
    if (worldPos.y <= 15.0f) {
        return true;
    }
    
    // Building generation using grid pattern
    int buildingX = static_cast<int>(worldPos.x / 16) * 16; // 16x16 building plots
    int buildingZ = static_cast<int>(worldPos.z / 16) * 16;
    
    // Use building position as seed for height
    std::mt19937 gen(seed + hash(buildingX, 0, buildingZ));
    std::uniform_int_distribution<int> heightDist(20, 60);
    int buildingHeight = heightDist(gen);
    
    // Check if we're in the building area (leave some space for roads)
    bool inBuildingX = (static_cast<int>(worldPos.x) % 16) >= 2 && (static_cast<int>(worldPos.x) % 16) <= 13;
    bool inBuildingZ = (static_cast<int>(worldPos.z) % 16) >= 2 && (static_cast<int>(worldPos.z) % 16) <= 13;
    
    if (inBuildingX && inBuildingZ && worldPos.y <= buildingHeight && worldPos.y > 15) {
        return true;
    }
    
    return false;
}

// The gradient-noise implementation now lives in the file-scope tn* free functions above
// (single source of truth, seed passed explicitly so it's reusable by the coarse-model source
// and the ridged detail). These members delegate, preserving identical behavior — including the
// deliberately-unmasked lattice index that keeps noise continuous across chunk edges / x=z=0.
float WorldGenerator::perlinNoise3D(float x, float y, float z, int octaves, float persistence, float lacunarity) {
    return tnFbm(x, y, z, octaves, persistence, lacunarity, seed);
}

float WorldGenerator::noise3D(float x, float y, float z) { return tnNoise3D(x, y, z, seed); }
float WorldGenerator::fade(float t) { return tnFade(t); }
float WorldGenerator::lerp(float a, float b, float t) { return tnLerp(a, b, t); }
float WorldGenerator::grad(int hash, float x, float y, float z) { return tnGrad(hash, x, y, z); }
int WorldGenerator::hash(int x, int y, int z) { return tnHash(x, y, z, seed); }

std::string WorldGenerator::getMaterialForPosition(const glm::ivec3& worldPos, float surfaceHeight) const {
    float y = static_cast<float>(worldPos.y);
    
    // City generation uses special materials
    if (generationType == GenerationType::City) {
        if (y <= 15.0f) {
            // Ground layer
            if (y <= 12.0f) return "Stone";
            return "Cobblestone"; // Road/ground surface (was "Default" → magenta missing-texture)
        }
        // Building blocks - use Metal for buildings
        return "Metal";
    }
    
    // Random generation - just use Default for everything
    if (generationType == GenerationType::Random) {
        return "Default";
    }
    
    // Natural terrain material assignment based on depth from surface
    float depthFromSurface = surfaceHeight - y;

    if (depthFromSurface < 0.5f) {
        // Surface layer: grass-topped dirt
        // Mountains above 45 get snow (Ice)
        if (surfaceHeight > 45.0f && generationType == GenerationType::Mountains) {
            return "Ice";
        }
        return "Grass";
    } else if (depthFromSurface < 4.0f) {
        // Dirt layer (just under surface)
        return "Dirt";
    } else if (depthFromSurface < terrainParams.stoneLevel) {
        // Mid layer - transition to stone
        return "Stone";
    } else {
        // Deep underground - stone
        return "Stone";
    }
}

} // namespace Phyxel
