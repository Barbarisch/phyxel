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

WorldGenerator::WorldGenerator(GenerationType type, uint32_t seed)
    : generationType(type), seed(seed) {
    initDefaultBiomes();
    // Best-effort override from resources/biomes.json (CWD-relative, like materials.json).
    // Keeps the built-in defaults if the file is missing or invalid.
    loadBiomes("resources/biomes.json");
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

float WorldGenerator::surfaceVariationFor(int wx, int wz) {
    // Terrain "bumpiness" around the base level (16). Per-biome height scale/offset and
    // continentalness are applied on top in sampleColumn. Flat contributes no variation.
    switch (generationType) {
        case GenerationType::Flat:
            return 0.0f;
        case GenerationType::Mountains:
            return perlinNoise3D(wx * 0.01f, 0.0f, wz * 0.01f, 6, 0.7f, 2.0f) * 40.0f
                 + perlinNoise3D(wx * 0.03f, 0.0f, wz * 0.03f, 4, 0.5f, 2.0f) * 20.0f
                 + 4.0f;
        case GenerationType::Perlin:
        case GenerationType::Caves:
        default:
            return perlinNoise3D(wx * terrainParams.frequency, 0.0f, wz * terrainParams.frequency,
                                 terrainParams.octaves, terrainParams.persistence, terrainParams.lacunarity)
                 * terrainParams.heightScale;
    }
}

WorldGenerator::ColumnSample WorldGenerator::sampleColumn(int wx, int wz) {
    ColumnSample col;
    // Low-frequency climate fields, decorrelated via distinct sample offsets, mapped to [0,1].
    // Continentalness is lower-frequency still (continents larger than biomes).
    auto to01 = [](float n) { float v = (n + 1.0f) * 0.5f; return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
    const float cf = terrainParams.climateFrequency;
    const float contF = cf * 0.42f;  // continents larger than biomes
    // Domain warp: offset the climate sample position by a separate noise so biome borders
    // are organic and wandering instead of straight Voronoi contours. 4 climate octaves add
    // fine detail so borders are ragged, not clean curves.
    const float warpAmp = 16.0f;
    const float warpF = cf * 2.0f;
    const float swx = wx + perlinNoise3D(wx * warpF, 500.0f, wz * warpF, 2, 0.5f, 2.0f) * warpAmp;
    const float swz = wz + perlinNoise3D(wx * warpF, 600.0f, wz * warpF, 2, 0.5f, 2.0f) * warpAmp;
    col.temperature     = to01(perlinNoise3D(swx * cf,   100.0f, swz * cf,   4, 0.5f, 2.0f));
    col.moisture        = to01(perlinNoise3D(swx * cf,   200.0f, swz * cf,   4, 0.5f, 2.0f));
    col.continentalness = to01(perlinNoise3D(wx * contF, 300.0f, wz * contF, 2, 0.5f, 2.0f));

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
        float dt = col.temperature - ct, dm = col.moisture - cm;
        float w = std::exp(-(dt * dt + dm * dm) / kSigma2);
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
        col.surfaceY = 16;  // Flat stays flat; biome affects material only (no cliffs, clean biome map)
    } else {
        float variation = surfaceVariationFor(wx, wz);
        float elevation = (col.continentalness - 0.5f) * 18.0f;  // large-scale land height, +-9 (gentle)
        col.surfaceY = static_cast<int>(std::floor(16.0f + variation * blendScale + blendOffset + elevation));
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

bool WorldGenerator::floraCell(int cx, int cz, FloraPlacement& out) {
    if (m_biomes.empty()) return false;
    auto hashu = [](int a, int b, uint32_t salt) -> uint32_t {
        uint32_t h = static_cast<uint32_t>(a) * 374761393u + static_cast<uint32_t>(b) * 668265263u
                   + salt * 2246822519u;
        h = (h ^ (h >> 13)) * 1274126177u; h ^= h >> 16; return h;
    };
    auto h01 = [&](int a, int b, uint32_t s) { return (hashu(a, b, s) & 0xFFFFFFu) / static_cast<float>(0x1000000); };

    // Jittered site within the cell (so plants aren't on a visible lattice).
    const int jx = cx * kFloraGrid + static_cast<int>(hashu(cx, cz, seed ^ 0xA1u) % kFloraGrid);
    const int jz = cz * kFloraGrid + static_cast<int>(hashu(cx, cz, seed ^ 0xB2u) % kFloraGrid);

    ColumnSample col = sampleColumn(jx, jz);
    const Biome& biome = m_biomes[col.biomeIndex];
    if (biome.flora.empty() || biome.floraDensity <= 0.0f) return false;

    // Local-maximum (Poisson-disk approximation) test: this cell wins only if its priority hash
    // beats every cell within the biome's spacing radius. Pure function of cell coords + seed →
    // order-independent, so a streamed chunk and a whole-region pass place identical trees.
    const int spacing = std::max(2, biome.floraSpacing);
    const int R = (spacing + kFloraGrid - 1) / kFloraGrid;
    const uint32_t p = hashu(cx, cz, seed ^ 0x9E3779B9u);
    for (int nz = cz - R; nz <= cz + R; ++nz)
        for (int nx = cx - R; nx <= cx + R; ++nx) {
            if (nx == cx && nz == cz) continue;
            const uint32_t q = hashu(nx, nz, seed ^ 0x9E3779B9u);
            if (q > p || (q == p && (nz < cz || (nz == cz && nx < cx)))) return false;  // neighbor wins
        }

    // Density thinning of the spacing-separated winners.
    if (h01(jx, jz, seed ^ 0xC3u) >= biome.floraDensity) return false;

    // Weighted template pick.
    int total = 0;
    for (const auto& f : biome.flora) total += f.second;
    int pick = static_cast<int>(h01(jx, jz, seed ^ 0xD4u) * total);
    const std::string* chosen = &biome.flora.front().first;
    for (const auto& f : biome.flora) { pick -= f.second; if (pick < 0) { chosen = &f.first; break; } }

    out = FloraPlacement{*chosen, jx, col.surfaceY, jz};
    out.procedural = (biome.floraMode == "procedural");
    out.fullness = biome.floraFullness;
    return true;
}

std::vector<WorldGenerator::FloraPlacement>
WorldGenerator::planFlora(int colMinX, int colMinZ, int colMaxX, int colMaxZ, int edgeInset) {
    std::vector<FloraPlacement> out;
    if (m_biomes.empty() || !isHeightBased()) return out;

    const int x0 = colMinX + edgeInset, x1 = colMaxX - edgeInset;
    const int z0 = colMinZ + edgeInset, z1 = colMaxZ - edgeInset;
    if (x1 < x0 || z1 < z0) return out;

    auto floordiv = [](int a, int b) { return (a >= 0) ? a / b : -((-a + b - 1) / b); };
    for (int cz = floordiv(z0, kFloraGrid); cz <= floordiv(z1, kFloraGrid); ++cz) {
        for (int cx = floordiv(x0, kFloraGrid); cx <= floordiv(x1, kFloraGrid); ++cx) {
            FloraPlacement p;
            if (floraCell(cx, cz, p) && p.worldX >= x0 && p.worldX <= x1 &&
                p.worldZ >= z0 && p.worldZ <= z1)
                out.push_back(std::move(p));
        }
    }
    LOG_DEBUG_FMT("WorldGenerator", "planFlora: " << out.size() << " plants over ["
                  << x0 << ".." << x1 << "]x[" << z0 << ".." << z1 << "]");
    return out;
}

void WorldGenerator::initDefaultBiomes() {
    // name, surface, subsurface, deep, tempMin, tempMax, moistMin, moistMax, heightScale, heightOffset.
    // Selection is nearest climate-cell CENTRE; height params blend smoothly across biomes.
    // ...heightScale, heightOffset, surfaceAlt, surfaceAltChance.
    m_biomes = {
        {"Snow",    "Ice",        "Stone",     "Stone", 0.0f, 0.3f, 0.0f, 1.0f,  1.3f,  6.0f, "",      0.0f},
        {"Desert",  "Sand",       "Sandstone", "Stone", 0.6f, 1.0f, 0.0f, 0.35f, 0.5f, -2.0f, "",      0.0f},
        {"Savanna", "GrassSavanna","Dirt",     "Stone", 0.7f, 1.0f, 0.35f, 0.6f, 0.7f,  0.0f, "Dirt",  0.3f},
        {"Forest",  "GrassForest","Dirt",      "Stone", 0.3f, 0.7f, 0.6f, 1.0f,  1.0f,  1.0f, "Dirt",  0.6f},
        {"Plains",  "Grass",      "Dirt",      "Stone", 0.0f, 1.0f, 0.0f, 1.0f,  0.6f,  0.0f, "",      0.0f},
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
            break;
        }
    }
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

float WorldGenerator::perlinNoise3D(float x, float y, float z, int octaves, float persistence, float lacunarity) {
    float total = 0.0f;
    float frequency = 1.0f;
    float amplitude = 1.0f;
    float maxValue = 0.0f;
    
    for (int i = 0; i < octaves; ++i) {
        total += noise3D(x * frequency, y * frequency, z * frequency) * amplitude;
        maxValue += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }
    
    return total / maxValue;
}

float WorldGenerator::noise3D(float x, float y, float z) {
    // Gradient-noise lattice. NOTE: do NOT mask the lattice index with & 255 here — the
    // corner hashes use xi+1/yi+1/zi+1 unmasked, so masking xi creates a hard discontinuity
    // at every multiple of 256 (e.g. world coordinate 0): the cell below 0 interpolates
    // toward hash(256) while the cell above starts from hash(0), which differ. That produced
    // a straight several-voxel cliff exactly along the x=0 / z=0 chunk edges. The hash takes
    // arbitrary ints, so leaving the index unmasked keeps the noise continuous everywhere.
    int xi = static_cast<int>(std::floor(x));
    int yi = static_cast<int>(std::floor(y));
    int zi = static_cast<int>(std::floor(z));
    
    float xf = x - std::floor(x);
    float yf = y - std::floor(y);
    float zf = z - std::floor(z);
    
    float u = fade(xf);
    float v = fade(yf);
    float w = fade(zf);
    
    int aaa = hash(xi, yi, zi);
    int aba = hash(xi, yi + 1, zi);
    int aab = hash(xi, yi, zi + 1);
    int abb = hash(xi, yi + 1, zi + 1);
    int baa = hash(xi + 1, yi, zi);
    int bba = hash(xi + 1, yi + 1, zi);
    int bab = hash(xi + 1, yi, zi + 1);
    int bbb = hash(xi + 1, yi + 1, zi + 1);
    
    float x1 = lerp(grad(aaa, xf, yf, zf), grad(baa, xf - 1, yf, zf), u);
    float x2 = lerp(grad(aba, xf, yf - 1, zf), grad(bba, xf - 1, yf - 1, zf), u);
    float y1 = lerp(x1, x2, v);
    
    x1 = lerp(grad(aab, xf, yf, zf - 1), grad(bab, xf - 1, yf, zf - 1), u);
    x2 = lerp(grad(abb, xf, yf - 1, zf - 1), grad(bbb, xf - 1, yf - 1, zf - 1), u);
    float y2 = lerp(x1, x2, v);
    
    return lerp(y1, y2, w);
}

float WorldGenerator::fade(float t) {
    return t * t * t * (t * (t * 6 - 15) + 10);
}

float WorldGenerator::lerp(float a, float b, float t) {
    return a + t * (b - a);
}

float WorldGenerator::grad(int hash, float x, float y, float z) {
    int h = hash & 15;
    float u = h < 8 ? x : y;
    float v = h < 4 ? y : h == 12 || h == 14 ? x : z;
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

int WorldGenerator::hash(int x, int y, int z) {
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
    
    y = ((y >> 16) ^ y) * 0x45d9f3b;
    y = ((y >> 16) ^ y) * 0x45d9f3b;
    y = (y >> 16) ^ y;
    
    z = ((z >> 16) ^ z) * 0x45d9f3b;
    z = ((z >> 16) ^ z) * 0x45d9f3b;
    z = (z >> 16) ^ z;
    
    return (x ^ y ^ z) + seed;
}

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
