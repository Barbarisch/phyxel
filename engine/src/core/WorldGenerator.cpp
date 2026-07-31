#include "core/WorldGenerator.h"
#include "core/WorldRecipe.h"
#include "core/MapCoarseSource.h"
#include "core/Chunk.h"
#include "core/Cube.h"
#include "core/HydrologyMap.h"
#include "core/FinePonds.h"
#include "core/FlowField.h"
#include "core/WorldConstants.h"
#include "utils/Logger.h"
#include <algorithm>
#include <random>
#include <cmath>
#include <climits>
#include <iostream>
#include <string>
#include <fstream>
#include <mutex>
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
// kSeaLevelY moved to core/WorldConstants.h — water sim + sea-plane renderer must share it.
using Core::kSeaLevelY;

// Ridged multifractal (Musgrave, musgrave.c): H=1.0, offset=1.0, gain=2.0; lacunarity=2.0 and
// octaves=6 from the libnoise/SharpNoise lineage (octaves is a tunable knob, not a fact).
constexpr float kRmH = 1.0f, kRmOffset = 1.0f, kRmGain = 2.0f, kRmLacunarity = 2.0f;
constexpr int   kRmOctaves = 6;
// Mountain relief is TWO ridged bands so peaks SLOPE up instead of forming sheer columns:
//  • a BROAD massif (long wavelength, most of the height) → a tall peak spreads its rise over
//    ~half a wavelength, giving flanks around the angle of repose, not vertical cliffs;
//  • a FINE detail band (short wavelength, small amplitude) → rocky ruggedness on the slopes.
// The old single 0.006-freq × 288-amp put a 288-voxel rise over ~83 units (~74° = sheer cliff).
constexpr float kRmFreqBroad = 0.0013f;   // massif form, ~770-unit wavelength (the SLOPE)
constexpr float kRmFreq      = 0.0060f;    // fine detail, ~167-unit wavelength (the roughness)

// Vertical scale (user decision 2026-07-09): COMPRESSED. Grandest peaks ~384 voxels above sea
// level; the continental base supplies up to +96, the broad+fine relief the rest.
constexpr float kContinentalMax = 96.0f;   // max low-frequency landmass rise above sea level (voxels)
constexpr float kContinentalMin = -40.0f;  // ocean/shelf floor below sea level (near-shore band; deep-ocean cap is P1/P2)
constexpr float kMtnBroadAmp  = 250.0f;    // Mountains: broad massif amplitude (the sloped bulk)
constexpr float kMtnFineAmp   = 55.0f;     // Mountains: fine rocky detail on the slopes
constexpr float kHillBroadAmp = 55.0f;     // Perlin/Caves: gentle rolling swells
constexpr float kHillFineAmp  = 22.0f;     // Perlin/Caves: light surface roughness

// ── P2 hydrology bake region (docs/TerrainGenerationV2.md §P2 "bounded backing"). ──
// The lake/river network is baked ONCE per world build over a fixed box centred on the world
// origin: 256 cells × 32 m = 8192 world units (~8 km) per side. Columns outside get no water/rivers
// (a DESIGN limit — infinite-world region partitioning is P5). 256² = 65 536 cells → the two
// Priority-Flood + accumulation passes are O(n log n), a few ms, run once (ctor + applyRecipe).
constexpr int   kHydroCells  = 256;
constexpr float kHydroCell   = 128.0f;     // 4 chunks per hydrology cell (coarse grid → cheap bake)
constexpr float kHydroOrigin = -0.5f * kHydroCells * kHydroCell;  // -16384 → box [-16384, 16384]² (~32 km)

// Valley shaping: a river's floor is planed smooth over a corridor several channel-widths wide, so
// the carved channel seats in a valley instead of a thin slot buried by mountain relief roughness.
// This multiplies the channel HALF-width, so the full smoothed corridor = kValleyWidthMul × the full
// bankfull channel width. GROUNDED (grounding-auditor 2026-07-10): unconfined alluvial valleys run
// well above the Rosgen (1994, *Catena* 22:169-199) entrenchment-ratio threshold of 2.2× bankfull
// width, and Williams (1986, *J. Hydrology* 88:147-164; meander-belt B = 3.7·W^1.12 over ~150 world
// stations) predicts ≈5.2× for an order-3 channel rising to ≈6.2× for order-6. 5.0× sits at the
// conservative-central end of that empirical range (a flat multiplier; the per-order ratio drift is
// minor and channelHalfWidth already scales the absolute width). (docs/TerrainGenerationV2.md §P2)
constexpr float kValleyWidthMul = 5.0f;

// River meander: the drainage runs on a 32 m D8 cell grid, so raw channels are straight, axis-aligned
// segments. To make rivers SINUOUS we domain-warp the query coordinate before every channel lookup —
// warp⁻¹ of a straight tube is a sinuous tube, so the SAME warp on the valley-shaping and carve queries
// bends both together without touching the drainage topology (downstream/order/flow stay intact).
// GROUNDED (grounding-auditor 2026-07-10):
//  • kMeanderFreq → wavelength λ = 1/0.018 ≈ 55 m ≈ 11× the ~5 m order-3 channel width — Leopold &
//    Wolman 1960 (USGS PP 282-B) λ ≈ 10.9·W; the 7–14×W band puts 55 m centrally.
//  • kMeanderAmp scales tnFbm, whose typical magnitude is well below its ±1 extreme (empirically
//    ~0.2–0.3), so the typical lateral displacement ≈ 55·0.25 ≈ 14 m. That matches the meander
//    amplitude ≈ half the belt width from Williams 1986 (*J. Hydrology* 88; belt B = 3.7·W^1.12 →
//    ≈26 m for a 5 m channel → amplitude ≈13 m). The actual sinuosity is validated by the
//    RiversMeander L3 test (not asserted from the noise magnitude alone).
constexpr float kMeanderFreq = 0.018f;
constexpr float kMeanderAmp  = 55.0f;

// ── P1 material rules (docs/TerrainGenerationV2.md §P1; grounding-auditor 2026-07-09). ──
// Temperature field anchor: normalized [0,1] == mean-annual −5..+30 °C (Whittaker 1975 biome-
// diagram temperature axis; 35 °C span). DESIGN DECISION (stated) — the citable anchor that lets
// the snow line and alpine gate fall out of real physics instead of a hand-picked Y threshold.
constexpr float kTempSpanC   = 35.0f;
constexpr float kSnowTemp01  = 5.0f / kTempSpanC;   // 0 °C freezing == (0−(−5))/35 ≈ 0.143 normalized
// Alpine treeline: BELOW this effective temperature the ground is bare permanent snowpack (no trees);
// BETWEEN kTreelineTemp01 and kSnowTemp01 snow lies on forested ground (taiga — boreal conifers grow
// well below 0 °C, so snow cover and trees coexist here). −8 °C mean-annual proxy for the cold-limit
// of trees (Körner alpine-treeline synthesis; growing-season heat sum). Bounded-by-analogy in this
// compressed seasonless model; sets the SnowGrass(taiga) → Snow(bare cap) split, not just appearance.
constexpr float kTreelineTemp01 = -3.0f / kTempSpanC;  // −8 °C == (−8−(−5))/35 ≈ −0.086 normalized
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

// Default continentalness → base-elevation shaping spline: a smoothstep ramp from the ocean/shelf
// floor (kSeaLevelY+kContinentalMin) to the high interior plateau (kSeaLevelY+kContinentalMax).
// Because Spline interpolates with smoothstep, this 2-point ramp is byte-identical to the old
// hardcoded continentalBase (flat lowlands/shelf, exaggerated interior); the Layer-1 ridged detail
// supplies the dramatic peaks on top. A world recipe may swap in a richer curve.
Spline defaultContinentalHeightSpline() {
    return Spline::ramp(0.0f, kSeaLevelY + kContinentalMin, 1.0f, kSeaLevelY + kContinentalMax);
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

// Forward decl: rebuildCoarseModel (below) bakes hydrology on the full surface via reliefAt, which
// is defined later next to surfaceVariationFor. Declared here (in namespace Phyxel, matching the
// static definition's linkage) so the bake can call it before its definition.
static float reliefAt(WorldGenerator::GenerationType genType, uint32_t seed, int wx, int wz, float cont);

WorldGenerator::WorldGenerator(GenerationType type, uint32_t seed)
    : generationType(type), seed(seed) {
    initDefaultBiomes();
    // Best-effort override from resources/biomes.json (CWD-relative, like materials.json).
    // Keeps the built-in defaults if the file is missing or invalid.
    loadBiomes("resources/biomes.json");
    m_continentalHeightSpline = defaultContinentalHeightSpline();
    rebuildCoarseModel();
}

// Build the Layer-0 coarse model. The source is PURE (captures only seed by value) so the
// worker's generator copy shares a valid, thread-safe model. cellSize = 32 (one sample per
// chunk): continentalness is very low frequency, so interpolating it per-chunk is effectively
// exact. Climate (temp/moisture) stays per-column in sampleColumn for P0; it migrates into the
// coarse model in P1 (biome overhaul), where biome-border changes are expected and tested.
// ── In-process hydrology-bake cache ─────────────────────────────────────────────────────────────
// The bake (HydrologyMap + FlowField) is a PURE function of (generationType, seed, climateFrequency
// → continentalness frequency, the height spline) over the fixed region constants — expensive
// (~0.6 s Debug) but IDENTICAL across generators with the same inputs. The test suite constructs the
// same (seed, type) many times in one process, and an in-session world reload reconstructs the
// generator; memoize so they reuse one bake instead of re-running Priority-Flood each time. The
// backings are immutable + const-read, so sharing the shared_ptrs across generators is safe (exactly
// like the streaming worker's generator copy already does). NOT persisted across process restarts —
// the bake is cheap enough that a fresh world-load re-bake is fine; world.db persistence is deferred.
namespace {
struct HydroBakeKey {
    int genType;
    uint32_t seed;
    float climateFreq;
    float seaLevel;   // the flood outlet is part of the bake's identity (same seed, different sea → different lakes)
    std::vector<Spline::Point> spline;
    bool operator==(const HydroBakeKey& o) const {
        if (genType != o.genType || seed != o.seed || climateFreq != o.climateFreq ||
            seaLevel != o.seaLevel) return false;
        if (spline.size() != o.spline.size()) return false;
        for (size_t i = 0; i < spline.size(); ++i)
            if (spline[i].x != o.spline[i].x || spline[i].y != o.spline[i].y) return false;
        return true;
    }
};
struct HydroBake {
    std::shared_ptr<HydrologyMap> hydro;
    std::shared_ptr<FlowField> flow;
    std::shared_ptr<WaterBodyIndex> bodies;   // tangible-water Phase A: body identity rides the bake
};
std::mutex g_hydroCacheMutex;
std::vector<std::pair<HydroBakeKey, HydroBake>> g_hydroCache;  // small: a handful of distinct configs
constexpr size_t kHydroCacheCap = 64;
}  // namespace

void WorldGenerator::clearHydroBakeCache() {
    std::lock_guard<std::mutex> lock(g_hydroCacheMutex);
    g_hydroCache.clear();
}

void WorldGenerator::setHeightmapSource(std::shared_ptr<const MapCoarseData> src) {
    m_mapSource = std::move(src);
    rebuildCoarseModel();
}

void WorldGenerator::rebuildCoarseModel() {
    clearColumnCache();  // memoized samples derive from the model being rebuilt
    m_finePondCache.clear();        // fine ponds derive from the columns (Phase B)
    m_finePondCacheOrder.clear();
    // Layer-0 override (P4): an imported heightmap drives base elevation directly. The map's
    // rivers/valleys are already baked into the height, so we skip the procedural hydrology
    // bake (m_hydro/m_flow stay null; sampleColumn's carve is guarded on m_flow). cellSize =
    // blocksPerPixel so the coarse grid aligns to map pixels; the SourceFunc reads nearest and
    // CoarseWorldModel bilinearly interpolates between pixel corners (one smooth interpolation).
    if (m_mapSource) {
        m_coarse = std::make_shared<CoarseWorldModel>(makeMapCoarseSource(m_mapSource),
                                                      m_mapSource->blocksPerPixel);
        m_hydro.reset();
        m_flow.reset();
        m_waterBodies.reset();
        return;
    }

    const uint32_t s = seed;
    // Continentalness is much lower-frequency than biome climate — its wavelength sets the landmass
    // size, which caps drainage-basin size and thus the max Strahler river order. At climateFrequency
    // 0.002 the 0.075 factor gives ~1/(0.002·0.075) ≈ 6.7 km landmasses: ~5 across the ~32 km bake
    // region, enough for order-5/6 trunk rivers to converge. DESIGN/ART-DIRECTION choice (a COMPRESSED
    // "continent" scale, like kSeaLevelY — NOT a geographic figure; real continents are ~1000s of km),
    // picked for landmass VARIETY across the region + big-river drainage. (Was 0.42 → ~1.2 km, order 3.)
    const float contF = terrainParams.climateFrequency * 0.075f;  // continents ≫ biomes
    // Capture the height spline BY VALUE so the coarse source stays pure (safe under the worker's
    // generator copy). Reshaping the spline (default vs recipe) changes the coastline/plateau profile.
    const Spline hspline = m_continentalHeightSpline;
    m_coarse = std::make_shared<CoarseWorldModel>(
        [s, contF, hspline](float x, float z) {
            CoarseSample cs;
            auto to01 = [](float n) { return n < -1.0f ? 0.0f : (n > 1.0f ? 1.0f : (n + 1.0f) * 0.5f); };
            // Expand contrast so continents genuinely reach ocean (low) and mountainous-interior
            // (high) extremes instead of grey mush (see expandContrast).
            cs.continentalness = expandContrast(to01(tnFbm(x * contF, 300.0f, z * contF, 2, 0.5f, 2.0f, s)));
            cs.baseHeight = hspline.eval(cs.continentalness);   // Layer-0 base via the shaping spline
            // temperature/moisture left at defaults here (sampleColumn computes them per-column for
            // biome selection).
            return cs;
        },
        32.0f);

    // ── P2 hydrology bake ─────────────────────────────────────────────────────────────────────
    // Flat / non-height-based types have no relief and no drainage worth baking → skip (leaves the
    // members null; sampleColumn's carve is guarded on m_flow).
    if (!isHeightBased() || generationType == GenerationType::Flat) {
        m_hydro.reset();
        m_flow.reset();
        m_waterBodies.reset();
        return;
    }
    // Cache lookup: the bake is fully determined by these inputs (region constants are compile-time).
    const float seaLvl = terrainParams.seaLevelY;
    const HydroBakeKey key{static_cast<int>(generationType), seed, terrainParams.climateFrequency,
                           seaLvl, m_continentalHeightSpline.points()};
    {
        std::lock_guard<std::mutex> lock(g_hydroCacheMutex);
        for (const auto& e : g_hydroCache)
            if (e.first == key) {
                m_hydro = e.second.hydro;
                m_flow = e.second.flow;
                m_waterBodies = e.second.bodies;
                return;
            }
    }
    // Height function for the flood/accumulation = the FULL rendered surface (Layer-0 coarse base +
    // Layer-1 relief): the relief's defined ridge/valley structure funnels drainage into convergent,
    // high-Strahler-order trunk rivers (the smooth base alone drains in low-order parallel sheets).
    // Pure: captures the immutable coarse model + seed + type by value; no `this`.
    auto heightAt = [coarse = m_coarse, s = seed, gt = generationType](float x, float z) -> float {
        const CoarseSample cs = coarse->sample(x, z);
        return cs.baseHeight + reliefAt(gt, s, static_cast<int>(std::floor(x)),
                                        static_cast<int>(std::floor(z)), cs.continentalness);
    };
    // Channel-initiation threshold is a physical AREA (kDefaultRiverThresholdCells ≈ 0.1 km² at the
    // 32 m reference cell); rescale it to THIS cell size so a coarser grid keeps the same real drainage
    // density (and the same Strahler order, which depends on basin-area/threshold-area, not on cell
    // resolution). Coarser cells → far fewer cells → an affordable bake over a much larger region.
    const int riverThresh = std::max(
        1, static_cast<int>(std::lround(FlowField::kDefaultRiverThresholdCells *
                                        (32.0 * 32.0) / (kHydroCell * kHydroCell))));
    m_hydro = std::make_shared<HydrologyMap>(heightAt, kHydroOrigin, kHydroOrigin,
                                             kHydroCells, kHydroCells, kHydroCell, seaLvl);
    m_flow  = std::make_shared<FlowField>(heightAt, kHydroOrigin, kHydroOrigin,
                                          kHydroCells, kHydroCells, kHydroCell, seaLvl, riverThresh);
    // Body identity rides the bake (tangible-water Phase A). Built HERE because volumeEst needs
    // the flood's height function, which is not retained anywhere after this scope.
    m_waterBodies = std::make_shared<WaterBodyIndex>(*m_hydro, heightAt);
    LOG_INFO_FMT("WorldGenerator", "[WORLD_GENERATOR] Water bodies labeled: "
             << m_waterBodies->bodies().size() << " bodies");
    LOG_INFO_FMT("WorldGenerator", "[WORLD_GENERATOR] Hydrology baked: " << kHydroCells << "x" << kHydroCells
             << " cells, seaLevel=" << seaLvl << ", maxAccum=" << m_flow->maxAccum()
             << " maxOrder=" << m_flow->maxOrder()
             << " drainageComplete=" << (m_flow->drainageComplete() ? 1 : 0));
    // Loud misconfiguration guard (docs/WaterPhysicalFeelPlan.md §2e): terrain that never reaches
    // sea level gives Priority-Flood no ocean outlet, so the whole region is one closed basin that
    // fills to its spill — lakes perch on hillsides and every downstream water diagnosis is chasing
    // a config error. Say so HERE, at bake time, instead of letting it surface as a "water bug".
    if (!m_hydro->hasOutlet()) {
        LOG_WARN_FMT("WorldGenerator", "[WORLD_GENERATOR] Hydrology bake has NO sea outlet: min terrain "
                 << m_hydro->minTerrain() << " sits ABOVE seaLevel " << seaLvl
                 << ". Every basin fills to its spill (perched hillside lakes). Set game.json "
                    "water.seaLevel to a level the terrain actually reaches.");
    }
    // Store in the cache (another thread may have baked the same key meanwhile — keep the first, they
    // are identical). Bounded: evict oldest once over the cap (few distinct configs in practice).
    {
        std::lock_guard<std::mutex> lock(g_hydroCacheMutex);
        for (const auto& e : g_hydroCache)
            if (e.first == key) return;  // someone else inserted it; ours is identical, drop it
        g_hydroCache.push_back({key, {m_hydro, m_flow, m_waterBodies}});
        if (g_hydroCache.size() > kHydroCacheCap) g_hydroCache.erase(g_hydroCache.begin());
    }
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
    //
    // Phase 4.4 fast path: a chunk that sits ENTIRELY at depth >= 4 under every column, with
    // one deep material and nothing to carve, is one uniform fill — not 32,768 addCube calls.
    // This is what keeps generated buried chunks in the uniform store representation so the
    // sealed classifier (ChunkManager) can retire them; it also removes the generation cost
    // that motivated the streamer's vertical clamp (loadChunksAroundPosition).
    const auto colsPtr = columnsForChunk(glm::ivec2(chunkCoord.x, chunkCoord.z));
    const std::vector<ColumnSample>& cols = *colsPtr;
    int minSurface = INT_MAX;
    for (const ColumnSample& c : cols) minSurface = std::min(minSurface, c.surfaceY);
    {
        const int chunkTop = chunkCoord.y * 32 + 31;
        const int chunkBottom = chunkCoord.y * 32;
        const bool noBedrockHere = !depthProfile.hasBedrock || depthProfile.bedrockY < chunkBottom;
        if (generationType != GenerationType::Caves && noBedrockHere &&
            chunkTop <= minSurface - 4) {
            static const std::string kFallbackDeep = "Stone";
            const std::string* deepMat = nullptr;
            bool uniformDeep = true;
            for (const ColumnSample& c : cols) {
                const std::string& dm =
                    m_biomes.empty() ? kFallbackDeep : m_biomes[c.biomeIndex].deepMaterial;
                if (!deepMat) deepMat = &dm;
                else if (*deepMat != dm) { uniformDeep = false; break; }
            }
            if (uniformDeep && deepMat) {
                chunk.fillAllCubes(*deepMat);
                LOG_TRACE_FMT("WorldGenerator", "[WORLD_GENERATOR] Uniform deep fill ("
                          << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z
                          << ") material=" << *deepMat);
                return;
            }
        }
    }
    for (int x = 0; x < 32; ++x) {
        for (int z = 0; z < 32; ++z) {
            int wx = chunkCoord.x * 32 + x;
            int wz = chunkCoord.z * 32 + z;
            const ColumnSample& col = cols[static_cast<size_t>(x) * 32 + z];

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

                // Creek bed recess (water-as-terrain-stage P2): the surface voxel of an inner-band
                // creek column is a 2-layer subcube shelf, not a full cube — the bed sits 1/3 voxel
                // below the banks, and the runtime's fractional ribbon pin rests IN the recess
                // (Chunk::subVoxelFloor reads 2/3; the water sim floors the cell accordingly).
                if (col.creekBed && wy == col.surfaceY) {
                    const std::string& shelfMat = materialForColumn(wy, col);
                    for (int sy = 0; sy < 2; ++sy)
                        for (int sx = 0; sx < 3; ++sx)
                            for (int sz = 0; sz < 3; ++sz)
                                chunk.addSubcube(localPos, glm::ivec3(sx, sy, sz), shelfMat);
                    continue;
                }

                chunk.addCube(localPos, materialForColumn(wy, col));
            }
        }
    }

    LOG_TRACE_FMT("WorldGenerator", "[WORLD_GENERATOR] Generated chunk (" << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z
              << ") column-first, biome-aware");
}

std::shared_ptr<const std::vector<WorldGenerator::ColumnSample>>
WorldGenerator::columnsForChunk(const glm::ivec2& colChunk) {
    const uint64_t key = (uint64_t(uint32_t(colChunk.x)) << 32) | uint64_t(uint32_t(colChunk.y));
    auto it = m_columnCache.find(key);
    if (it != m_columnCache.end()) return it->second;

    auto fresh = std::make_shared<std::vector<ColumnSample>>();
    fresh->reserve(32 * 32);
    for (int x = 0; x < 32; ++x)
        for (int z = 0; z < 32; ++z)
            fresh->push_back(sampleColumn(colChunk.x * 32 + x, colChunk.y * 32 + z));

    if (m_columnCache.size() >= kColumnCacheMax) {   // FIFO eviction
        m_columnCache.erase(m_columnCacheOrder.front());
        m_columnCacheOrder.erase(m_columnCacheOrder.begin());
    }
    m_columnCache.emplace(key, fresh);
    m_columnCacheOrder.push_back(key);
    return fresh;
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

// Layer-1 ridged relief at a column — PURE (a free function of generation type + seed + position +
// continentalness), so both surfaceVariationFor and the hydrology bake (rebuildCoarseModel) compute
// the SAME surface without a `this` capture or a duplicated copy of the math that could drift.
static float reliefAt(WorldGenerator::GenerationType genType, uint32_t seed, int wx, int wz, float cont) {
    // Layer-1 mountain relief (docs/TerrainGenerationV2.md P0): ridged multifractal, domain-
    // warped so ridgelines bend, gated by a "mountainousness" mask from continentalness so
    // plains stay flat and only high continental interiors grow rough peaks. Returns voxels of
    // relief >= 0 (the continental base + sea level are added in sampleColumn). Flat = no relief.
    if (genType == WorldGenerator::GenerationType::Flat) return 0.0f;

    // Domain warp the sample position (Quilez: fbm(p + fbm(p))) so ridges braid organically.
    const float warpF = 0.006f, warpAmp = 40.0f;
    const float wxw = wx + tnFbm(wx * warpF, 900.0f, wz * warpF, 2, 0.5f, 2.0f, seed) * warpAmp;
    const float wzw = wz + tnFbm(wx * warpF, 950.0f, wz * warpF, 2, 0.5f, 2.0f, seed) * warpAmp;

    // Two ridged bands: a BROAD massif (the sloped bulk of a mountain) + FINE detail (roughness).
    // Summed, a peak's height is reached gradually over the broad wavelength → flanks slope up
    // instead of jumping vertically. The fine band is decorrelated by a distinct seed.
    const float broad = clamp01(tnRidgedMultifractal(wxw * kRmFreqBroad, wzw * kRmFreqBroad, seed) / kRidgedNorm);
    const float fine  = clamp01(tnRidgedMultifractal(wxw * kRmFreq, wzw * kRmFreq, seed ^ 0x51EDu) / kRidgedNorm);

    // Mountainousness mask (continentalness passed in by the caller): low continental land is
    // gentle, high continental interior is alpine. Mountains bias the whole map upward and rougher;
    // Perlin/Caves get gentler rolling hills.
    if (genType == WorldGenerator::GenerationType::Mountains) {
        const float mask = smoothstep01(0.20f, 0.70f, cont);
        return (broad * kMtnBroadAmp + fine * kMtnFineAmp) * mask;
    }
    // Perlin / Caves: gentler, hills emerge above mid-continentalness.
    const float mask = smoothstep01(0.40f, 0.85f, cont);
    return (broad * kHillBroadAmp + fine * kHillFineAmp) * mask;
}

float WorldGenerator::surfaceVariationFor(int wx, int wz, float cont) {
    return reliefAt(generationType, seed, wx, wz, cont);
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
        // Deliberately the ENGINE constant, not terrainParams.seaLevelY: Flat worlds are authored
        // against "solid below Y=16", and a water.seaLevel override must not move their ground.
        col.surfaceY = static_cast<int>(kSeaLevelY);  // Flat stays flat; biome affects material only
    } else {
        // surfaceY = Layer-0 continental base (sea level + landmass rise/ocean carve)
        //          + Layer-1 ridged mountain relief, scaled by the biome's height extremeness.
        // The old ±9 continental cap is gone — the base now spans kContinentalMin..kContinentalMax
        // and peaks reach ~kSeaLevelY + kContinentalMax + kMountainAmp (~384 above sea level).
        float relief = surfaceVariationFor(wx, wz, coarse.continentalness);

        // P2 valley shaping (docs/TerrainGenerationV2.md §P2): near a river, attenuate Layer-1 relief
        // toward the channel centreline so the river seats in a SMOOTH valley floor rather than a thin
        // slot buried by relief roughness. Relief is >= 0, so driving it to 0 at the thalweg makes the
        // channel the local minimum by construction. The corridor is a few channel-widths wide
        // (kValleyWidthMul); outside it, relief is untouched. Guarded on m_flow.
        // MEANDER: warp the query coordinate so straight D8 cell-channels carve as SINUOUS rivers in
        // world space. The SAME warped (mwx,mwz) drives BOTH the valley shaping and the carve below, so
        // valley and channel stay aligned. Two decorrelated fbm bands (distinct offsets) give an x/z
        // displacement of up to ~kMeanderAmp; smooth, so the meandered channel is continuous (no seam).
        float mwx = static_cast<float>(wx), mwz = static_cast<float>(wz);
        float creekSwale = 0.0f;   // bounded creek dip (water-as-terrain-stage P2), voxels
        if (m_flow) {
            const glm::vec2 m = meanderedChannelPos(static_cast<float>(wx), static_cast<float>(wz));
            mwx = m.x;
            mwz = m.y;
        }

        // P2 valley shaping (docs/TerrainGenerationV2.md §P2): attenuate Layer-1 relief toward the
        // (meandered) channel centreline so the river seats in a SMOOTH valley floor, not a thin slot.
        if (m_flow) {
            const float maxValleyHalf = FlowField::channelHalfWidth(6) * kValleyWidthMul;  // widest order
            const FlowField::NearestChannel nc = m_flow->nearestChannel(mwx, mwz, maxValleyHalf);
            if (nc.order >= 3) {
                const float valleyHalf = FlowField::channelHalfWidth(nc.order) * kValleyWidthMul;
                relief *= smoothstep01(0.0f, valleyHalf, nc.dist);  // 0 at centreline → full at edge
            }

            // Creek SWALE (water-as-terrain-stage P2): orders 1-2 get a NARROW, BOUNDED parabolic
            // dip — a swale the creek lies in, over a band ~2 channel-widths wide, aligned with the
            // ribbon via the same meandered (mwx,mwz). This is what makes a creek read as shaped BY
            // the terrain instead of painted across it. The dip is an ABSOLUTE depth (~1.6 voxels
            // at the centreline), deliberately NOT a fraction of relief: a fractional attenuation
            // (the big rivers' valley rule) scaled by mountain relief cut measured 55-voxel slot
            // canyons along 3-voxel-wide creeks — a fraction is only safe over a wide valley.
            const float creekSwaleHalf = FlowField::channelHalfWidth(2) * 2.0f;   // 3 voxels
            const FlowField::NearestChannel cs = m_flow->nearestChannel(mwx, mwz, creekSwaleHalf, 1);
            if (cs.order >= 1 && cs.order <= 2 && cs.dist < creekSwaleHalf) {
                constexpr float kCreekSwaleDepth = 1.6f;
                const float t = cs.dist / creekSwaleHalf;
                creekSwale = kCreekSwaleDepth * (1.0f - t * t);
            }
        }
        col.surfaceY = static_cast<int>(std::floor(coarse.baseHeight + relief * blendScale + blendOffset - creekSwale));

        // P2 river carve: lower the smooth valley floor further where the network runs a channel
        // (order ≥ 3; orders 1-2 are sub-voxel → no bed), a parabolic bed (deepest at the centreline).
        // Done BEFORE the material overrides so a carved bed that dips below sea level reads as seabed
        // Sand. riverOrder marks the bed for the flora gate + the water runtime (WaterSystemV2 fills
        // the channel; the carve only shapes the terrain).
        if (m_flow) {
            const FlowField::ChannelHit ch = m_flow->channelAt(mwx, mwz);
            if (ch.hit) {
                // TERRAIN carve is order ≥ 3 ONLY. Orders 1-2 (creeks) report fractional WATER
                // depths for the runtime's ribbon pin — an order-2 centreline (0.66) would lround
                // to a full-voxel trench here, which is exactly the wrong outcome for a sub-voxel
                // creek. riverOrder is still recorded for every order: the water runtime and the
                // flora gate both consume it (no trees standing in the creek line).
                if (ch.order >= 3) col.surfaceY -= static_cast<int>(std::lround(ch.depth));
                col.riverOrder = ch.order;
                // Creek BED RECESS (water-as-terrain-stage P2): the inner band of an order 1-2
                // channel — same 0.15 depth threshold the runtime pin uses (WaterManager::
                // applyRiverInflows), so every pinned ribbon cell gets a recess and the parabolic
                // band edges get neither. generateChunk emits the surface voxel as a 2-layer
                // subcube shelf (floor 2/3): the ribbon rests 1/3 voxel below its banks.
                col.creekBed = (ch.order <= 2 && ch.depth >= 0.15f);
            }
        }

        // P1 slope + altitude/temperature material overrides (docs/TerrainGenerationV2.md §P1).
        // These layer physical surfacing ON TOP of the biome material: a sand seabed below sea
        // level, exposed rock past the angle of repose, and a lapse-rate snow line. Moderate,
        // gently-sloped land keeps its biome surface. (Flat is exempt — it stays a clean biome map.)
        // Per-world sea level (terrainParams.seaLevelY): the material gates below must agree with
        // the hydrology bake's outlet, or seabed sand / the snow line disagree with where water
        // actually sits. (kSeaLevelY remains the default when no world override exists.)
        const int altitude = col.surfaceY - static_cast<int>(terrainParams.seaLevelY);
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

        if (col.surfaceY < static_cast<int>(terrainParams.seaLevelY)) {
            col.surfaceMat = "Sand";    // ocean floor / seabed (water itself arrives in P2)
        } else if (slope > kRockSlope) {
            col.surfaceMat = "Stone";   // too steep for soil to hold → exposed rock / scree
        } else if (effTemp < kTreelineTemp01) {
            col.surfaceMat = "Snow";       // above treeline: bare permanent snowpack (blocks flora)
        } else if (effTemp < kSnowTemp01) {
            col.surfaceMat = "SnowGrass";  // snow lies on forested ground (taiga) — conifers persist
        }
        // else: keep the biome surface material set above (moderate, gently-sloped land).
    }
    return col;
}

// ── Fine-scale ponds (tangible-water Phase B) ────────────────────────────────────────────────

std::shared_ptr<const std::vector<WorldGenerator::StoredFinePond>>
WorldGenerator::finePondsForCell(int cellX, int cellZ) {
    const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(cellX)) << 32) |
                         static_cast<uint64_t>(static_cast<uint32_t>(cellZ));
    auto it = m_finePondCache.find(key);
    if (it != m_finePondCache.end()) return it->second;

    auto fresh = std::make_shared<std::vector<StoredFinePond>>();
    // No fine ponds without a bake (Flat/heightmap worlds keep authored water only).
    if (m_flow && m_hydro) {
        constexpr int kWin = 64, kMargin = 16;
        const int ox = cellX * 32 - kMargin, oz = cellZ * 32 - kMargin;
        auto floorDiv = [](int a, int b) { return a >= 0 ? a / b : (a - b + 1) / b; };
        // Heights via the chunk-column cache (shared with generation — hot when the area has
        // generated). Surface = top face of the surface voxel: water rests on it.
        auto heightAt = [&](int x, int z) -> float {
            const int wx = ox + x, wz = oz + z;
            const int ccx = floorDiv(wx, 32), ccz = floorDiv(wz, 32);
            const auto blk = columnsForChunk(glm::ivec2(ccx, ccz));
            const ColumnSample& c =
                (*blk)[static_cast<size_t>(wx - ccx * 32) * 32 + (wz - ccz * 32)];
            return static_cast<float>(c.surfaceY + 1);
        };
        int index = 0;
        for (const FinePond& p : discoverFinePonds(heightAt, kWin, kWin)) {
            // OWNERSHIP: the cell containing the deepest column owns the pond — every window
            // that fully contains the basin computes it identically, and exactly one cell
            // claims it (basins near a cell edge whose window can't contain them are dropped
            // by the border rule; accepted coverage loss, documented).
            const glm::ivec2 deepW(ox + p.deepest.x, oz + p.deepest.y);
            if (floorDiv(deepW.x, 32) != cellX || floorDiv(deepW.y, 32) != cellZ) continue;
            // Reject overlap with baked water or a carved channel — those columns already have
            // an owner (the bake's bodies / the river ribbon).
            const float cx = ox + (p.bboxMin.x + p.bboxMax.x + 1) * 0.5f;
            const float cz = oz + (p.bboxMin.y + p.bboxMax.y + 1) * 0.5f;
            if (m_hydro->hasWater(cx, cz) ||
                m_hydro->hasWater(static_cast<float>(deepW.x), static_cast<float>(deepW.y)))
                continue;
            if (channelHitAt(static_cast<float>(deepW.x) + 0.5f,
                             static_cast<float>(deepW.y) + 0.5f).hit)
                continue;

            StoredFinePond sp;
            // Namespaced above bake-body ids; deterministic in (cell, discovery order).
            sp.id = (int64_t(1) << 40) |
                    (static_cast<int64_t>(static_cast<uint16_t>(cellX)) << 24) |
                    (static_cast<int64_t>(static_cast<uint16_t>(cellZ)) << 8) |
                    static_cast<int64_t>(index++);
            sp.level = p.level;
            sp.bboxMinW = glm::ivec2(ox + p.bboxMin.x, oz + p.bboxMin.y);
            sp.bboxMaxW = glm::ivec2(ox + p.bboxMax.x, oz + p.bboxMax.y);
            sp.columns.reserve(p.columns.size());
            for (uint32_t pc : p.columns) {
                const int lx = static_cast<int>(pc >> 16), lz = static_cast<int>(pc & 0xffffu);
                sp.columns.push_back(
                    (static_cast<uint64_t>(static_cast<uint32_t>(ox + lx)) << 32) |
                    static_cast<uint64_t>(static_cast<uint32_t>(oz + lz)));
            }
            std::sort(sp.columns.begin(), sp.columns.end());
            fresh->push_back(std::move(sp));
        }
    }
    if (m_finePondCache.size() >= kFinePondCacheMax) {   // FIFO eviction (column-cache pattern)
        m_finePondCache.erase(m_finePondCacheOrder.front());
        m_finePondCacheOrder.erase(m_finePondCacheOrder.begin());
    }
    m_finePondCache.emplace(key, fresh);
    m_finePondCacheOrder.push_back(key);
    return fresh;
}

WorldGenerator::FinePondHit WorldGenerator::finePondAt(int worldX, int worldZ) {
    FinePondHit hit;
    if (!m_flow || !m_hydro) return hit;
    auto floorDiv = [](int a, int b) { return a >= 0 ? a / b : (a - b + 1) / b; };
    const int cellX = floorDiv(worldX, 32), cellZ = floorDiv(worldZ, 32);
    const uint64_t colKey = (static_cast<uint64_t>(static_cast<uint32_t>(worldX)) << 32) |
                            static_cast<uint64_t>(static_cast<uint32_t>(worldZ));
    // A pond's columns lie within its owner's ±16-margin window, so the owner is one of the
    // 3×3 cells around this column's cell.
    for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx) {
            const auto ponds = finePondsForCell(cellX + dx, cellZ + dz);
            for (const StoredFinePond& sp : *ponds) {
                if (worldX < sp.bboxMinW.x || worldX > sp.bboxMaxW.x ||
                    worldZ < sp.bboxMinW.y || worldZ > sp.bboxMaxW.y)
                    continue;
                if (std::binary_search(sp.columns.begin(), sp.columns.end(), colKey)) {
                    hit.id = sp.id;
                    hit.level = sp.level;
                    return hit;
                }
            }
        }
    return hit;
}

glm::vec2 WorldGenerator::meanderedChannelPos(float wx, float wz) const {
    // The ONE meander warp (docs/TerrainGenerationV2.md §P2): everything that touches the channel
    // line — carve, valley, swale, bed shelf (sampleColumn) AND the water runtime's ribbon queries
    // (channelHitAt) — must go through this same displacement, or the water lands beside its bed.
    return glm::vec2(
        wx + kMeanderAmp * tnFbm(wx * kMeanderFreq, 71.0f, wz * kMeanderFreq, 2, 0.5f, 2.0f, seed ^ 0x9271u),
        wz + kMeanderAmp * tnFbm(wx * kMeanderFreq, 131.0f, wz * kMeanderFreq, 2, 0.5f, 2.0f, seed ^ 0x9271u));
}

FlowField::ChannelHit WorldGenerator::channelHitAt(float worldX, float worldZ) const {
    if (!m_flow) return {};
    const glm::vec2 m = meanderedChannelPos(worldX, worldZ);
    return m_flow->channelAt(m.x, m.y);
}

glm::vec2 WorldGenerator::channelFlowDirAt(float worldX, float worldZ) const {
    if (!m_flow) return glm::vec2(0.0f);
    const glm::vec2 m = meanderedChannelPos(worldX, worldZ);
    // Only claim a direction where there IS a channel (a spill on open ground must not read as a
    // river); direction itself is cell-granular, sampled at the warped position for consistency.
    return m_flow->channelAt(m.x, m.y).hit ? m_flow->flowDirAt(m.x, m.y) : glm::vec2(0.0f);
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
    // don't grow on the seabed, bare-rock cliffs, or snow above the treeline. The lapse-rate override
    // stamps "Snow" (pure alpine snowpack) on ANY column above the treeline -- including the Snow
    // biome's own high ground -- and that blocks flora. The Snow biome's lower ground is "SnowGrass"
    // (snow-dusted soil), which is NOT gated, so boreal conifers still grow there. sampleColumn
    // already applied the override to col.surfaceMat. (docs/TerrainGenerationV2.md §P1)
    if (col.surfaceY < static_cast<int>(terrainParams.seaLevelY)) return false; // seabed / underwater (per-world sea level)
    if (col.surfaceMat == "Stone") return false;                                // cliff (slope override; no biome surfaces Stone)
    if (col.surfaceMat == "Snow") return false;                                 // alpine permanent-snow cap (above treeline)
    // P2: no trees in a carved river channel, nor on land that sits below a lake/sea surface (the
    // water runtime will flood it). Keeps flora off the water line. (docs/TerrainGenerationV2.md §P2)
    if (col.riverOrder > 0) return false;                                       // carved riverbed
    if (m_hydro) {
        const float wl = m_hydro->waterLevelAt(static_cast<float>(jx), static_cast<float>(jz));
        if (wl > HydrologyMap::NO_WATER * 0.5f && static_cast<float>(col.surfaceY) < wl) return false;  // under lake/sea
    }

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

bool WorldGenerator::faunaCell(int cx, int cz, FaunaPlacement& out) {
    if (m_biomes.empty()) return false;
    auto hashu = [](int a, int b, uint32_t salt) -> uint32_t {
        uint32_t h = static_cast<uint32_t>(a) * 374761393u + static_cast<uint32_t>(b) * 668265263u
                   + salt * 2246822519u;
        h = (h ^ (h >> 13)) * 1274126177u; h ^= h >> 16; return h;
    };
    auto h01 = [&](int a, int b, uint32_t s) { return (hashu(a, b, s) & 0xFFFFFFu) / static_cast<float>(0x1000000); };

    // Fauna-specific salt so herd anchors are statistically INDEPENDENT of flora placement
    // (an animal isn't forced onto or away from a tree cell).
    const uint32_t fsalt = 0x5EED1234u;

    // Jittered site within the cell (shared kFloraGrid) so herds aren't on a visible lattice.
    const int jx = cx * kFloraGrid + static_cast<int>(hashu(cx, cz, seed ^ (0xA1u ^ fsalt)) % kFloraGrid);
    const int jz = cz * kFloraGrid + static_cast<int>(hashu(cx, cz, seed ^ (0xB2u ^ fsalt)) % kFloraGrid);

    ColumnSample col = sampleColumn(jx, jz);
    const Biome& biome = m_biomes[col.biomeIndex];
    if (biome.fauna.empty() || biome.faunaDensity <= 0.0f) return false;

    // Same physical surface gates as flora: no seabed, cliff, alpine snow, riverbed, or land
    // under a lake/sea surface (animals shouldn't spawn on water or bare rock).
    if (col.surfaceY < static_cast<int>(terrainParams.seaLevelY)) return false;  // per-world sea level
    if (col.surfaceMat == "Stone" || col.surfaceMat == "Snow") return false;
    if (col.riverOrder > 0) return false;
    if (m_hydro) {
        const float wl = m_hydro->waterLevelAt(static_cast<float>(jx), static_cast<float>(jz));
        if (wl > HydrologyMap::NO_WATER * 0.5f && static_cast<float>(col.surfaceY) < wl) return false;
    }

    // Local-maximum (Poisson-disk) test over the biome's faunaSpacing radius — order-independent.
    const int spacing = std::max(2, biome.faunaSpacing);
    const int R = (spacing + kFloraGrid - 1) / kFloraGrid;
    const uint32_t psalt = (seed ^ 0x9E3779B9u) ^ fsalt;
    const uint32_t p = hashu(cx, cz, psalt);
    for (int nz = cz - R; nz <= cz + R; ++nz)
        for (int nx = cx - R; nx <= cx + R; ++nx) {
            if (nx == cx && nz == cz) continue;
            const uint32_t q = hashu(nx, nz, psalt);
            if (q > p || (q == p && (nz < cz || (nz == cz && nx < cx)))) return false;  // neighbor wins
        }

    // Density thinning of the spacing-separated winners.
    if (h01(jx, jz, seed ^ (0xC3u ^ fsalt)) >= biome.faunaDensity) return false;

    // Weighted animal pick.
    int total = 0;
    for (const auto& f : biome.fauna) total += f.second;
    int pick = static_cast<int>(h01(jx, jz, seed ^ (0xD4u ^ fsalt)) * total);
    const std::string* chosen = &biome.fauna.front().first;
    for (const auto& f : biome.fauna) { pick -= f.second; if (pick < 0) { chosen = &f.first; break; } }

    out = FaunaPlacement{*chosen, jx, col.surfaceY, jz};
    return true;
}

std::vector<WorldGenerator::FaunaPlacement>
WorldGenerator::planFauna(int colMinX, int colMinZ, int colMaxX, int colMaxZ, int edgeInset) {
    std::vector<FaunaPlacement> out;
    if (m_biomes.empty() || !isHeightBased()) return out;

    const int x0 = colMinX + edgeInset, x1 = colMaxX - edgeInset;
    const int z0 = colMinZ + edgeInset, z1 = colMaxZ - edgeInset;
    if (x1 < x0 || z1 < z0) return out;

    auto floordiv = [](int a, int b) { return (a >= 0) ? a / b : -((-a + b - 1) / b); };
    for (int cz = floordiv(z0, kFloraGrid); cz <= floordiv(z1, kFloraGrid); ++cz) {
        for (int cx = floordiv(x0, kFloraGrid); cx <= floordiv(x1, kFloraGrid); ++cx) {
            FaunaPlacement p;
            if (faunaCell(cx, cz, p) && p.worldX >= x0 && p.worldX <= x1 &&
                p.worldZ >= z0 && p.worldZ <= z1)
                out.push_back(std::move(p));
        }
    }
    LOG_DEBUG_FMT("WorldGenerator", "planFauna: " << out.size() << " herds over ["
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
        {"Snow",    "SnowGrass",  "Stone",     "Stone", 0.0f, 0.3f, 0.0f, 1.0f,  0.0f, 1.0f, 1.3f,  6.0f, "",      0.0f},
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
    r.seaLevelY = terrainParams.seaLevelY;
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
    // Snapshot the continental height spline so the world round-trips its terrain shape.
    for (const auto& p : m_continentalHeightSpline.points())
        r.heightSpline.push_back({p.x, p.y});
    return r;
}

void WorldGenerator::applyRecipe(const WorldRecipe& recipe) {
    terrainParams.climateFrequency = recipe.climateFrequency;
    terrainParams.seaLevelY = recipe.seaLevelY;   // rebake below re-floods against this outlet
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
    // A recipe may reshape the continentalness → base-elevation spline (coastline/plateau profile).
    // Empty → keep the current (default) spline so existing worlds are unaffected.
    if (!recipe.heightSpline.empty()) {
        std::vector<Spline::Point> pts;
        pts.reserve(recipe.heightSpline.size());
        for (const auto& p : recipe.heightSpline) pts.push_back({p.x, p.y});
        m_continentalHeightSpline = Spline(std::move(pts));
    }
    // climateFrequency and/or the height spline changed → the coarse model's inputs changed. Rebuild
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
        // Optional fauna pool: wandering animals scattered by the runtime FaunaSpawner. Mirrors
        // the flora block but sparser; "items" carry an animFile instead of a template.
        if (b.contains("fauna") && b["fauna"].is_object()) {
            const auto& fa = b["fauna"];
            biome.faunaDensity = fa.value("density", 0.0f);
            biome.faunaSpacing = fa.value("spacing", 48);
            if (fa.contains("items") && fa["items"].is_array()) {
                for (const auto& it : fa["items"]) {
                    std::string anim = it.value("animFile", "");
                    int weight = it.value("weight", 1);
                    if (!anim.empty() && weight > 0) biome.fauna.emplace_back(std::move(anim), weight);
                }
            }
        }
        loaded.push_back(std::move(biome));
    }
    if (loaded.empty()) return false;
    m_biomes = std::move(loaded);
    clearColumnCache();  // cached samples carry biomeIndex into this table
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
        // Mountains above 45 get snow-capped
        if (surfaceHeight > 45.0f && generationType == GenerationType::Mountains) {
            return "Snow";
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
