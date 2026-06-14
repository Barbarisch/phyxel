#include "core/WorldGenerator.h"
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

int WorldGenerator::surfaceHeightFor(int wx, int wz) {
    // Matches the legacy per-type height formulas so terrain shape is unchanged; floor(h)
    // is equivalent to the old `worldY <= h` test for integer worldY.
    switch (generationType) {
        case GenerationType::Flat:
            return 16;
        case GenerationType::Mountains: {
            float h = perlinNoise3D(wx * 0.01f, 0.0f, wz * 0.01f, 6, 0.7f, 2.0f) * 40.0f
                    + perlinNoise3D(wx * 0.03f, 0.0f, wz * 0.03f, 4, 0.5f, 2.0f) * 20.0f
                    + 20.0f;
            return static_cast<int>(std::floor(h));
        }
        case GenerationType::Perlin:
        case GenerationType::Caves:
        default: {
            float h = perlinNoise3D(wx * terrainParams.frequency, 0.0f, wz * terrainParams.frequency,
                                    terrainParams.octaves, terrainParams.persistence, terrainParams.lacunarity)
                      * terrainParams.heightScale + 16.0f;
            return static_cast<int>(std::floor(h));
        }
    }
}

WorldGenerator::ColumnSample WorldGenerator::sampleColumn(int wx, int wz) {
    ColumnSample col;
    col.surfaceY = surfaceHeightFor(wx, wz);
    // Low-frequency climate fields, decorrelated via distinct sample offsets, mapped to [0,1].
    auto to01 = [](float n) { float v = (n + 1.0f) * 0.5f; return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
    col.temperature = to01(perlinNoise3D(wx * 0.006f, 100.0f, wz * 0.006f, 2, 0.5f, 2.0f));
    col.moisture    = to01(perlinNoise3D(wx * 0.006f, 200.0f, wz * 0.006f, 2, 0.5f, 2.0f));
    col.biomeIndex  = selectBiome(col.temperature, col.moisture);
    return col;
}

int WorldGenerator::selectBiome(float temperature, float moisture) const {
    // First climate cell that contains (temperature, moisture) wins; the table's last entry
    // should be a catch-all. (Hard borders for now; height-blending is a follow-up.)
    for (size_t i = 0; i < m_biomes.size(); ++i) {
        const Biome& b = m_biomes[i];
        if (temperature >= b.tempMin && temperature <= b.tempMax &&
            moisture   >= b.moistMin && moisture   <= b.moistMax)
            return static_cast<int>(i);
    }
    return 0;
}

std::string WorldGenerator::materialForColumn(int worldY, const ColumnSample& col) const {
    if (m_biomes.empty()) return "Stone";
    const Biome& b = m_biomes[col.biomeIndex];
    const int depth = col.surfaceY - worldY;  // 0 at the surface
    if (depth <= 0) return b.surfaceMaterial;
    if (depth < 4)  return b.subsurfaceMaterial;
    return b.deepMaterial;
}

void WorldGenerator::initDefaultBiomes() {
    // name, surface, subsurface, deep, tempMin, tempMax, moistMin, moistMax.
    // Order = priority (first match wins); the last entry is the catch-all.
    m_biomes = {
        {"Snow",    "Ice",   "Stone",     "Stone", 0.0f, 0.3f, 0.0f, 1.0f},
        {"Desert",  "Sand",  "Sandstone", "Stone", 0.6f, 1.0f, 0.0f, 0.35f},
        {"Savanna", "Grass", "Dirt",      "Stone", 0.7f, 1.0f, 0.35f, 0.6f},
        {"Forest",  "Grass", "Dirt",      "Stone", 0.3f, 0.7f, 0.6f, 1.0f},
        {"Plains",  "Grass", "Dirt",      "Stone", 0.0f, 1.0f, 0.0f, 1.0f},
    };
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
    // Simple 3D noise implementation (can be replaced with better implementation)
    int xi = static_cast<int>(std::floor(x)) & 255;
    int yi = static_cast<int>(std::floor(y)) & 255;
    int zi = static_cast<int>(std::floor(z)) & 255;
    
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
            return "Default"; // Road/ground surface
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
