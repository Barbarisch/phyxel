#include "core/GameDefinitionLoader.h"
#include "core/ChunkManager.h"
#include "core/WorldGenerator.h"
#include "core/WorldRecipe.h"
#include "core/MapCoarseSource.h"
#include "core/WorldStorage.h"
#include "core/StructureGenerator.h"
#include "core/StructureBuildService.h"
#include "core/NPCManager.h"
#include "core/CharacterVisualResolver.h"
#include "core/EntityRegistry.h"
#include "core/ObjectTemplateManager.h"
#include "core/VoxelTemplate.h"
#include "core/GameEventLog.h"
#include "core/TriggerSystem.h"
#include "core/HealthComponent.h"
#include "core/LocationRegistry.h"
#include "core/MaterialRegistry.h"
#include "core/PlacedObjectManager.h"
#include "scene/NPCEntity.h"
#include "scene/AnimatedVoxelCharacter.h"
#include "scene/behaviors/ScheduledBehavior.h"
#include "scene/behaviors/BehaviorTreeBehavior.h"
#include "scene/behaviors/StoryDrivenBehavior.h"
#include "scene/behaviors/CombatBehavior.h"
#include "scene/behaviors/RangedCasterBehavior.h"
#include "ai/CommandStructure.h"
#include "ai/Schedule.h"
#include "ai/BTLoader.h"
#include "graphics/Camera.h"
#include "ui/DialogueSystem.h"
#include "ui/DialogueData.h"
#include "story/StoryEngine.h"
#include "story/StoryWorldLoader.h"
#include "story/StoryDirectorTypes.h"
#include "story/CharacterProfile.h"
#include "utils/Logger.h"

#include <unordered_set>
#include <algorithm>
#include <cmath>

namespace Phyxel {
namespace Core {

// ============================================================================
// GameDefinitionResult
// ============================================================================

json GameDefinitionResult::toJson() const {
    json j;
    j["success"] = success;
    if (!error.empty()) j["error"] = error;
    j["chunks_generated"] = chunksGenerated;
    j["structures_placed"] = structuresPlaced;
    j["npcs_spawned"] = npcsSpawned;
    j["locations_registered"] = locationsRegistered;
    j["player_spawned"] = playerSpawned;
    j["camera_set"] = cameraSet;
    j["story_loaded"] = storyLoaded;
    return j;
}

// ============================================================================
// Validation
// ============================================================================

std::pair<bool, std::string> GameDefinitionLoader::validate(const json& definition) {
    if (!definition.is_object()) {
        return {false, "Game definition must be a JSON object"};
    }

    // Validate world section
    if (definition.contains("world")) {
        const auto& world = definition["world"];
        if (!world.is_object()) return {false, "'world' must be an object"};
        if (world.contains("type")) {
            std::string type = world["type"].get<std::string>();
            if (type != "Random" && type != "Perlin" && type != "Flat" &&
                type != "Mountains" && type != "Caves" && type != "City") {
                return {false, "Invalid world type: " + type};
            }
        }
    }

    // Validate NPCs
    if (definition.contains("npcs")) {
        if (!definition["npcs"].is_array()) return {false, "'npcs' must be an array"};
        for (size_t i = 0; i < definition["npcs"].size(); i++) {
            const auto& npc = definition["npcs"][i];
            if (!npc.contains("name") || !npc["name"].is_string()) {
                return {false, "NPC at index " + std::to_string(i) + " missing 'name'"};
            }
        }
    }

    // Validate player
    if (definition.contains("player")) {
        if (!definition["player"].is_object()) return {false, "'player' must be an object"};
    }

    // Validate triggers (per-entry validation happens in TriggerSystem::addTrigger)
    if (definition.contains("triggers")) {
        if (!definition["triggers"].is_array()) return {false, "'triggers' must be an array"};
    }

    // Validate structures
    if (definition.contains("structures")) {
        if (!definition["structures"].is_array()) return {false, "'structures' must be an array"};
        for (size_t i = 0; i < definition["structures"].size(); i++) {
            const auto& s = definition["structures"][i];
            if (!s.contains("type") || !s["type"].is_string()) {
                return {false, "Structure at index " + std::to_string(i) + " missing 'type'"};
            }
            std::string stype = s["type"].get<std::string>();
            // Allow fill, template, place_voxel, and procedural structure types
            static const std::unordered_set<std::string> validTypes = {
                "fill", "template", "place_voxel",
                "house", "tavern", "tower", "wall", "room", "box",
                "staircase", "table", "chair", "counter", "bed"
            };
            if (validTypes.find(stype) == validTypes.end()) {
                return {false, "Invalid structure type '" + stype + "' at index " + std::to_string(i)};
            }
            // Unknown material names render as a magenta missing-texture
            // checkerboard at runtime — catch them here instead. Skipped when
            // the registry isn't populated (e.g. bare unit tests).
            if (s.contains("material") && s["material"].is_string()) {
                const auto& reg = MaterialRegistry::instance();
                const std::string mat = s["material"].get<std::string>();
                if (reg.getMaterialCount() > 0 && !mat.empty() && !reg.hasMaterial(mat)) {
                    return {false, "Unknown material '" + mat + "' in structure at index " +
                            std::to_string(i) + " (names are case-sensitive; see list_materials)"};
                }
            }
        }
    }

    return {true, ""};
}

// ============================================================================
// Main Load Entry Point
// ============================================================================

GameDefinitionResult GameDefinitionLoader::load(const json& definition, GameSubsystems& subsystems) {
    GameDefinitionResult result;

    // Validate first
    auto [valid, validationError] = validate(definition);
    if (!valid) {
        result.error = validationError;
        return result;
    }

    std::string gameName = definition.value("name", "Untitled Game");
    LOG_INFO("GameDefinitionLoader", "Loading game definition: " + gameName);

    // Load each section in order: world → structures → player → camera → NPCs → story
    if (definition.contains("world")) {
        // The sea level the world generates against comes from the sibling "water" block — the
        // hydrology bake needs the SAME level the water runtime will fill to, or Priority-Flood
        // floods against the wrong outlet (perched hillside lakes; WaterPhysicalFeelPlan §2e).
        float bakeSeaLevelY = Core::kSeaLevelY;
        if (definition.contains("water") && definition["water"].is_object())
            bakeSeaLevelY = definition["water"].value("seaLevel", Core::kSeaLevelY);
        loadWorld(definition["world"], bakeSeaLevelY, subsystems, result);
        if (!result.error.empty()) return result;
    }

    if (definition.contains("structures")) {
        loadStructures(definition["structures"], subsystems, result);
        if (!result.error.empty()) return result;
    }

    if (definition.contains("player")) {
        loadPlayer(definition["player"], subsystems, result);
        if (!result.error.empty()) return result;
    }

    if (definition.contains("camera")) {
        loadCamera(definition["camera"], subsystems, result);
    }

    // "sky": the celestial bodies (graphics/CelestialBody.h). Passed through unparsed for the
    // layering reason above; absent means the caller keeps the default sun + moon.
    if (definition.contains("sky")) {
        result.skyDefinition = definition["sky"];
        result.skyLoaded = true;
    }

    if (definition.contains("locations")) {
        loadLocations(definition["locations"], subsystems, result);
        if (!result.error.empty()) return result;
    }

    if (definition.contains("npcs")) {
        loadNPCs(definition["npcs"], subsystems, result);
        if (!result.error.empty()) return result;
    }

    if (definition.contains("story")) {
        loadStory(definition["story"], subsystems, result);
        if (!result.error.empty()) return result;
    }

    // Declarative when/then triggers (win conditions, game-flow rules). A
    // definition that carries a "triggers" key REPLACES the active trigger set;
    // a definition without one leaves existing triggers alone (consistent with
    // the other optional sections).
    if (definition.contains("triggers") && subsystems.triggerSystem) {
        subsystems.triggerSystem->clear();
        int added = subsystems.triggerSystem->loadFromJson(definition["triggers"]);
        LOG_INFO("GameDefinitionLoader", "Triggers: loaded " + std::to_string(added));
    }

    result.success = true;
    LOG_INFO("GameDefinitionLoader", "Game definition loaded: " + gameName +
             " (chunks=" + std::to_string(result.chunksGenerated) +
             ", structures=" + std::to_string(result.structuresPlaced) +
             ", locations=" + std::to_string(result.locationsRegistered) +
             ", npcs=" + std::to_string(result.npcsSpawned) + ")");

    return result;
}

// ============================================================================
// World Generation
// ============================================================================

void GameDefinitionLoader::loadWorld(const json& worldDef, float bakeSeaLevelY, GameSubsystems& sub,
                                     GameDefinitionResult& result) {
    if (!sub.chunkManager) {
        result.error = "ChunkManager not available for world generation";
        return;
    }

    std::string typeStr = worldDef.value("type", "Perlin");
    uint32_t seed = worldDef.value("seed", 0u);

    WorldGenerator::GenerationType genType = WorldGenerator::GenerationType::Perlin;
    if (typeStr == "Random") genType = WorldGenerator::GenerationType::Random;
    else if (typeStr == "Flat") genType = WorldGenerator::GenerationType::Flat;
    else if (typeStr == "Mountains") genType = WorldGenerator::GenerationType::Mountains;
    else if (typeStr == "Caves") genType = WorldGenerator::GenerationType::Caves;
    else if (typeStr == "City") genType = WorldGenerator::GenerationType::City;

    WorldGenerator generator(genType, seed);

    // Layer-0 imported heightmap (docs/TerrainGenerationV2.md P4): { "heightmap": { "dir": ... } }
    // drives base elevation from tools/middle_earth/import_terrain.py output (1:1 map → world).
    // Loaded once; shared (immutable) with the streaming generator below.
    std::shared_ptr<MapCoarseData> mapData;
    if (worldDef.contains("heightmap") && worldDef["heightmap"].contains("dir")) {
        std::string err;
        mapData = MapCoarseData::load(worldDef["heightmap"]["dir"].get<std::string>(), err);
        if (mapData) generator.setHeightmapSource(mapData);
        else LOG_ERROR("GameDefinitionLoader", "World: heightmap load failed: " + err);
    }

    // Apply terrain params
    if (worldDef.contains("params")) {
        auto& tp = generator.getTerrainParams();
        const auto& p = worldDef["params"];
        if (p.contains("heightScale")) tp.heightScale = p["heightScale"].get<float>();
        if (p.contains("frequency")) tp.frequency = p["frequency"].get<float>();
        if (p.contains("octaves")) tp.octaves = p["octaves"].get<int>();
        if (p.contains("persistence")) tp.persistence = p["persistence"].get<float>();
        if (p.contains("lacunarity")) tp.lacunarity = p["lacunarity"].get<float>();
        if (p.contains("caveThreshold")) tp.caveThreshold = p["caveThreshold"].get<float>();
        if (p.contains("stoneLevel")) tp.stoneLevel = p["stoneLevel"].get<float>();
        if (p.contains("climateFrequency")) tp.climateFrequency = p["climateFrequency"].get<float>();
    }
    // Sea level from the game.json "water" block (extracted by the caller) — set unconditionally
    // so the recipe snapshot below carries it and the bake floods against the right outlet.
    generator.getTerrainParams().seaLevelY = bakeSeaLevelY;

    // Per-world recipe (docs/WorldModel.md): the world DB is the source of truth for
    // generation tuning. If a recipe is stored, apply it (reproducible, immune to global
    // biomes.json edits); otherwise snapshot the current config (biomes.json + game.json params)
    // and persist it so future loads of this world are stable. Must run before generation +
    // streaming config so climateFrequency / biome tuning take effect.
    WorldRecipe recipe = generator.makeRecipe();
    // Test-world flora override (game.json world.floraOverride): rewrite EVERY biome's flora
    // tuning before the recipe is persisted, so lab worlds (e.g. TreeLodLab's lone-oak plain)
    // are reproducible from their game definition alone — no hand-edited world.db. Keys:
    //   spacing (int), density (float), template (string — replaces all items with one
    //   template), keepLayers (bool, default false — extra vegetation bands dropped).
    // Like every recipe field, a stored recipe WINS on later loads: changing the override
    // requires regenerating the world (delete world.db), same contract as seaLevel.
    if (worldDef.contains("floraOverride") && worldDef["floraOverride"].is_object()) {
        const auto& fo = worldDef["floraOverride"];
        for (auto& b : recipe.biomes) {
            if (fo.contains("spacing")) b.floraSpacing = fo["spacing"].get<int>();
            if (fo.contains("density")) b.floraDensity = fo["density"].get<float>();
            if (fo.contains("template")) {
                b.flora.clear();
                b.flora.push_back({fo["template"].get<std::string>(), 1});
                b.floraMode = "pool";
            }
            if (!fo.value("keepLayers", false)) b.extraLayers.clear();
        }
        LOG_INFO("GameDefinitionLoader", "World: floraOverride applied to " +
                 std::to_string(recipe.biomes.size()) + " biome tunes (test-world knob)");
    }
    // WorldForge params (docs/WorldForge.md): game.json world.worldforge → the recipe, so the
    // DB owns the plan config from first load. Presence of the block implies enabled unless it
    // says otherwise. Like every recipe field, a STORED recipe wins on later loads.
    if (worldDef.contains("worldforge") && worldDef["worldforge"].is_object()) {
        nlohmann::json wf = worldDef["worldforge"];
        if (!wf.contains("enabled")) wf["enabled"] = true;
        recipe.worldforge = WorldForgeParams::fromJson(wf);
        LOG_INFO("GameDefinitionLoader", "World: worldforge params from game.json (siteCount=" +
                 std::to_string(recipe.worldforge.siteCount) + ")");
    }
    if (WorldStorage* storage = sub.chunkManager ? sub.chunkManager->getWorldStorage() : nullptr) {
        if (storage->hasMeta("recipe")) {
            recipe = WorldRecipe::fromJson(storage->getMeta("recipe"));
            // The stored recipe's sea level WINS over game.json: the terrain carve, seabed
            // materials and hydrology table were generated against it, and re-flooding an
            // existing world at a different level makes them all lie. Warn loudly instead.
            if (std::abs(recipe.seaLevelY - bakeSeaLevelY) > 0.01f)
                LOG_WARN("GameDefinitionLoader", "World: game.json water.seaLevel " +
                         std::to_string(bakeSeaLevelY) + " != world.db recipe seaLevelY " +
                         std::to_string(recipe.seaLevelY) +
                         " — using the recipe value (the terrain was generated against it). "
                         "Regenerate the world to change its sea level.");
            generator.applyRecipe(recipe);
            LOG_INFO("GameDefinitionLoader", "World: applied generation recipe from world.db");
        } else {
            recipe.type = typeStr;
            if (storage->setMeta("recipe", recipe.toJson()))
                LOG_INFO("GameDefinitionLoader", "World: synthesized + persisted generation recipe to world.db");
            // Fresh world: apply the just-synthesized recipe so the coarse model + hydrology
            // bake are rebuilt with the game.json params (climateFrequency, seaLevelY). The
            // ctor baked with defaults; without this the up-front generation would use that
            // stale bake while streamed chunks (applyRecipe'd below) use the correct one.
            generator.applyRecipe(recipe);
        }
    } else {
        // No world storage (ephemeral host / tests): still rebake with the game.json params.
        generator.applyRecipe(recipe);
    }
    // The recipe may own a different seed than game.json (the DB is the source of truth;
    // applyRecipe adopts recipe.seed) — everything downstream (streaming config, logs) must
    // use the seed the generator actually runs with.
    seed = generator.getSeed();

    // Streaming terrain (Phase 1b): when enabled, chunks generate and evict around the
    // player at runtime instead of being limited to the up-front bounded set below. The
    // up-front gen still runs to guarantee solid ground at spawn; streaming extends beyond
    // it using the same generator type/seed/params, so the seam is continuous.
    if (worldDef.value("streaming", false)) {
        sub.chunkManager->configureStreamingGeneration(true, genType, seed);
        if (WorldGenerator* sg = sub.chunkManager->getStreamingGenerator()) {
            sg->getTerrainParams() = generator.getTerrainParams();
            sg->applyRecipe(recipe);   // streamed chunks use the same per-world tuning
            if (mapData) sg->setHeightmapSource(mapData);  // after applyRecipe (which rebuilds coarse)
        }
        // Wire per-chunk flora decoration for streamed chunks (clip-stamp; the generator is
        // fetched lazily so it's valid for the lifetime of streaming).
        if (sub.templateManager) {
            ObjectTemplateManager* otm = sub.templateManager;
            ChunkManager* cm = sub.chunkManager;
            cm->setFloraDecorator([otm, cm](Chunk& chunk, const glm::ivec3& coord) {
                if (WorldGenerator* g = cm->getStreamingGenerator())
                    otm->decorateChunk(chunk, coord, *g);
            });
            // Async generation: flora stamps on the WORKER with its private generator
            // (decorateChunk only reads the preloaded template library and writes the
            // passed chunk). Dense-forest chunks cost 450-625ms — off the main thread.
            cm->setWorkerFloraDecorator([otm](Chunk& chunk, const glm::ivec3& coord,
                                              WorldGenerator& workerGen) {
                otm->decorateChunk(chunk, coord, workerGen);
            });
        }
        if (worldDef.contains("loadRadius"))
            sub.chunkManager->loadDistance = worldDef["loadRadius"].get<float>() * 32.0f;
        if (worldDef.contains("unloadRadius"))
            sub.chunkManager->unloadDistance = worldDef["unloadRadius"].get<float>() * 32.0f;
        // Keep hysteresis: unload must be comfortably beyond load or chunks thrash.
        if (sub.chunkManager->unloadDistance <= sub.chunkManager->loadDistance + 32.0f)
            sub.chunkManager->unloadDistance = sub.chunkManager->loadDistance + 64.0f;
        LOG_INFO("GameDefinitionLoader", "World: streaming terrain ENABLED (loadDist=" +
                 std::to_string(sub.chunkManager->loadDistance) + ", unloadDist=" +
                 std::to_string(sub.chunkManager->unloadDistance) + ")");
    }

    // Pre-baked / previously saved worlds: the DB already holds the terrain, so stop
    // here — config above (recipe, params, streaming generation, radii) is applied,
    // but regeneration would overwrite saved edits and is very slow.
    if (sub.skipTerrainGeneration) {
        LOG_INFO("GameDefinitionLoader", "World: config applied; terrain generation skipped (chunks already in DB)");
        return;
    }

    // Collect chunk coordinates
    std::vector<glm::ivec3> chunkCoords;
    if (worldDef.contains("chunks")) {
        for (const auto& c : worldDef["chunks"]) {
            chunkCoords.emplace_back(c.value("x", 0), c.value("y", 0), c.value("z", 0));
        }
    } else if (worldDef.contains("from") && worldDef.contains("to")) {
        int fx = worldDef["from"].value("x", 0), fy = worldDef["from"].value("y", 0), fz = worldDef["from"].value("z", 0);
        int tx = worldDef["to"].value("x", 0), ty = worldDef["to"].value("y", 0), tz = worldDef["to"].value("z", 0);
        for (int cx = std::min(fx, tx); cx <= std::max(fx, tx); cx++)
            for (int cy = std::min(fy, ty); cy <= std::max(fy, ty); cy++)
                for (int cz = std::min(fz, tz); cz <= std::max(fz, tz); cz++)
                    chunkCoords.emplace_back(cx, cy, cz);
    } else {
        chunkCoords.emplace_back(0, 0, 0);
    }

    if (chunkCoords.size() > 64) {
        result.error = "Too many chunks (max 64), got " + std::to_string(chunkCoords.size());
        return;
    }

    std::unordered_set<Chunk*> modifiedChunks;
    for (const auto& cc : chunkCoords) {
        glm::ivec3 origin = cc * 32;
        if (!sub.chunkManager->getChunkAtCoord(cc))
            sub.chunkManager->createChunk(origin, false);
        Chunk* chunk = sub.chunkManager->getChunkAtCoord(cc);
        if (chunk) {
            generator.generateChunk(*chunk, cc);
            modifiedChunks.insert(chunk);
            result.chunksGenerated++;
        }
    }

    for (Chunk* chunk : modifiedChunks) {
        chunk->rebuildFaces();
        chunk->updateVulkanBuffer();
        chunk->forcePhysicsRebuild();
    }

    // Flora decoration pass (Phase 5): scatter biome-appropriate vegetation across the
    // generated region. Runs only for fixed-region worlds — every chunk exists now, so
    // ObjectTemplateManager::spawnTemplate routes a tree's overhang into the correct
    // neighbor chunk (no clipped trees at chunk seams). Streaming worlds need the
    // decorate-once-neighbors-present deferral and are skipped here for now. Opt out with
    // world.flora=false.
    if (sub.templateManager && worldDef.value("flora", true) && !worldDef.value("streaming", false)
        && !chunkCoords.empty()) {
        int minCX = chunkCoords[0].x, maxCX = chunkCoords[0].x;
        int minCZ = chunkCoords[0].z, maxCZ = chunkCoords[0].z;
        for (const auto& cc : chunkCoords) {
            minCX = std::min(minCX, cc.x); maxCX = std::max(maxCX, cc.x);
            minCZ = std::min(minCZ, cc.z); maxCZ = std::max(maxCZ, cc.z);
        }
        const int colMinX = minCX * 32, colMaxX = maxCX * 32 + 31;
        const int colMinZ = minCZ * 32, colMaxZ = maxCZ * 32 + 31;
        sub.templateManager->decorateFlora(generator, colMinX, colMinZ, colMaxX, colMaxZ);
    }

    LOG_INFO("GameDefinitionLoader", "World: generated " + std::to_string(result.chunksGenerated) +
             " chunks (" + typeStr + ", seed=" + std::to_string(seed) + ")");
}

// ============================================================================
// Structures
// ============================================================================

void GameDefinitionLoader::loadStructures(const json& structures, GameSubsystems& sub, GameDefinitionResult& result) {
    if (!sub.chunkManager) {
        result.error = "ChunkManager not available for structure placement";
        return;
    }

    for (const auto& s : structures) {
        std::string stype = s["type"].get<std::string>();

        if (stype == "fill") {
            int x1 = s["from"].value("x", 0), y1 = s["from"].value("y", 0), z1 = s["from"].value("z", 0);
            int x2 = s["to"].value("x", 0), y2 = s["to"].value("y", 0), z2 = s["to"].value("z", 0);
            std::string material = s.value("material", "");
            bool hollow = s.value("hollow", false);
            // "replace": true overwrites occupied voxels. Default fills only
            // place into empty air (addCube fails on occupied) — historically a
            // silent failure; we now count and log placed/failed either way.
            bool replace = s.value("replace", false);

            // Reject unknown materials loudly (they'd render as the magenta
            // missing-texture checkerboard). Registry-empty guard for bare tests.
            {
                const auto& reg = MaterialRegistry::instance();
                if (!material.empty() && reg.getMaterialCount() > 0 && !reg.hasMaterial(material)) {
                    LOG_ERROR("GameDefinitionLoader", "Skipping fill: unknown material '" + material +
                              "' (case-sensitive; see list_materials)");
                    continue;
                }
            }

            int minX = std::min(x1, x2), maxX = std::max(x1, x2);
            int minY = std::min(y1, y2), maxY = std::max(y1, y2);
            int minZ = std::min(z1, z2), maxZ = std::max(z1, z2);

            int64_t volume = (int64_t)(maxX - minX + 1) * (maxY - minY + 1) * (maxZ - minZ + 1);
            if (volume > 100000) {
                LOG_WARN("GameDefinitionLoader", "Skipping fill structure: volume " +
                         std::to_string(volume) + " exceeds 100k limit");
                continue;
            }

            int placed = 0, failed = 0;
            for (int ix = minX; ix <= maxX; ++ix) {
                for (int iy = minY; iy <= maxY; ++iy) {
                    for (int iz = minZ; iz <= maxZ; ++iz) {
                        if (hollow && ix > minX && ix < maxX &&
                            iy > minY && iy < maxY && iz > minZ && iz < maxZ) {
                            continue;
                        }
                        const glm::ivec3 pos(ix, iy, iz);
                        if (replace && sub.chunkManager->hasVoxelAt(pos)) {
                            // removeCubeFast defers the re-mesh (chunk marked
                            // dirty; per-frame updateDirtyChunks rebuilds once).
                            sub.chunkManager->removeCubeFast(pos);
                        }
                        bool ok = false;
                        if (!material.empty()) {
                            ok = sub.chunkManager->m_voxelModificationSystem.addCubeWithMaterial(
                                pos, material);
                        } else {
                            ok = sub.chunkManager->addCube(pos);
                        }
                        if (ok) placed++; else failed++;
                    }
                }
            }
            result.structuresPlaced++;
            // INFO so silent collisions are visible in any load log. failed>0 on
            // a non-replace fill almost always means it overlapped terrain or an
            // earlier fill — author wants "replace": true or different coords.
            LOG_INFO("GameDefinitionLoader", "Fill " + (material.empty() ? std::string("(default)") : material) +
                     " [" + std::to_string(minX) + "," + std::to_string(minY) + "," + std::to_string(minZ) +
                     "]..[" + std::to_string(maxX) + "," + std::to_string(maxY) + "," + std::to_string(maxZ) +
                     "]: placed " + std::to_string(placed) + ", failed " + std::to_string(failed) +
                     (failed > 0 && !replace ? " (occupied voxels skipped — add \"replace\": true to overwrite)" : ""));

        } else if (stype == "template") {
            if (!sub.templateManager) {
                LOG_WARN("GameDefinitionLoader", "Skipping template structure: ObjectTemplateManager not available");
                continue;
            }
            std::string templateName = s.value("template", "");
            if (templateName.empty()) {
                LOG_WARN("GameDefinitionLoader", "Skipping template structure: no template name");
                continue;
            }
            float x = s["position"].value("x", 0.0f);
            float y = s["position"].value("y", 0.0f);
            float z = s["position"].value("z", 0.0f);
            bool isStatic = !s.value("dynamic", false);

            // Use PlacedObjectManager if available for registry tracking
            bool spawned = false;
            if (sub.placedObjectManager) {
                int rot = s.value("rotation", 0);
                std::string parentId = s.value("parent_id", "");
                std::string objId = sub.placedObjectManager->placeTemplate(
                    templateName, glm::ivec3(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z)),
                    rot, parentId);
                spawned = !objId.empty();
            } else {
                spawned = sub.templateManager->spawnTemplate(templateName, glm::vec3(x, y, z), isStatic);
            }

            if (spawned) {
                result.structuresPlaced++;
                LOG_DEBUG("GameDefinitionLoader", "Template: spawned " + templateName);
            } else {
                LOG_WARN("GameDefinitionLoader", "Failed to spawn template: " + templateName);
            }

        } else if (stype == "house" || stype == "tavern") {
            // Generated buildings run the SAME engine pipeline as the API command
            // (Structure Generation v2 via StructureBuildService) — the legacy v1
            // composites were removed. Legacy params (width/depth/int stories) alias
            // onto a v2 typology; materials come from the style, not a palette.
            StructureBuildService::Deps deps;
            deps.chunkManager  = sub.chunkManager;
            deps.placedObjects = sub.placedObjectManager;
            deps.templates     = sub.templateManager;
            deps.locations     = sub.locationRegistry;
            deps.npcs          = sub.npcManager;
            nlohmann::json v2p = s.value("schema", std::string()) == "v2"
                ? s : StructureBuildService::aliasLegacyParams(s);
            nlohmann::json resp = StructureBuildService::buildV2(v2p, deps);
            if (resp.value("success", false)) {
                result.structuresPlaced++;
                LOG_INFO("GameDefinitionLoader", stype + " (v2): placed " +
                         std::to_string(resp.value("placed", 0)) + " voxels, " +
                         std::to_string(resp.value("fixtures_spawned", 0)) + " fixtures");
                if (resp.contains("locations"))
                    result.locationsRegistered += static_cast<int>(resp["locations"].size());
            } else {
                LOG_WARN("GameDefinitionLoader", "Failed to build " + stype + " (v2): " +
                         resp.value("error", std::string("unknown error")));
            }

        } else if (stype == "tower") {
            LOG_WARN("GameDefinitionLoader", "'tower' was removed with the v1 composite "
                     "generators (no v2 typology yet) — build it from primitives (wall/"
                     "staircase) or a template instead. Skipped.");

        } else if (stype == "wall" || stype == "room" || stype == "box" ||
                   stype == "staircase" || stype == "table" || stype == "chair" ||
                   stype == "counter" || stype == "bed") {
            // Primitive placements — deterministic StructureGenerator primitives
            auto structure = StructureGenerator::generateFromJson(s);
            auto placement = StructureGenerator::place(sub.chunkManager, structure);

            if (placement.placed > 0) {
                result.structuresPlaced++;
                LOG_DEBUG("GameDefinitionLoader", stype + ": placed " + std::to_string(placement.placed) +
                          " voxels (" + std::to_string(placement.failed) + " failed)");

                // Register with PlacedObjectManager so it shows in World Outliner
                if (sub.placedObjectManager && !structure.voxels.empty()) {
                    glm::ivec3 bboxMin(INT_MAX), bboxMax(INT_MIN);
                    for (const auto& v : structure.voxels) {
                        bboxMin = glm::min(bboxMin, v.position);
                        bboxMax = glm::max(bboxMax, v.position);
                    }
                    glm::ivec3 pos = structure.voxels[0].position;
                    std::string parentId = s.value("parent_id", "");
                    sub.placedObjectManager->registerStructure(stype, pos, 0, bboxMin, bboxMax, parentId);
                }
            } else {
                LOG_WARN("GameDefinitionLoader", "Failed to place " + stype + " structure");
            }

            // Auto-register location markers
            if (sub.locationRegistry) {
                for (auto& loc : placement.locations) {
                    if (loc.id.empty()) {
                        // Generate an ID from the structure type and position
                        loc.id = stype + "_" + std::to_string(static_cast<int>(loc.position.x)) +
                                 "_" + std::to_string(static_cast<int>(loc.position.z));
                    }
                    Location regLoc;
                    regLoc.id = loc.id;
                    regLoc.name = loc.name;
                    regLoc.position = loc.position;
                    regLoc.radius = loc.radius;
                    regLoc.type = loc.type;
                    sub.locationRegistry->addLocation(regLoc);
                    result.locationsRegistered++;
                    LOG_DEBUG("GameDefinitionLoader", "Auto-registered location '" + loc.id + "' from " + stype);
                }
            }
        } else if (stype == "place_voxel") {
            int x = s.value("x", 0);
            int y = s.value("y", 0);
            int z = s.value("z", 0);
            std::string material = s.value("material", "Default");
            std::string subcubeType = s.value("subcube", ""); // "sub", "micro", or empty for full cube

            bool ok = false;
            if (subcubeType == "sub") {
                int sx = s.value("sx", 1), sy = s.value("sy", 1), sz = s.value("sz", 1);
                ok = sub.chunkManager->m_voxelModificationSystem.addSubcubeWithMaterial(
                    glm::ivec3(x, y, z), glm::ivec3(sx, sy, sz), material);
            } else if (subcubeType == "micro") {
                int sx = s.value("sx", 1), sy = s.value("sy", 1), sz = s.value("sz", 1);
                int mx = s.value("mx", 1), my = s.value("my", 1), mz = s.value("mz", 1);
                ok = sub.chunkManager->m_voxelModificationSystem.addMicrocubeWithMaterial(
                    glm::ivec3(x, y, z), glm::ivec3(sx, sy, sz), glm::ivec3(mx, my, mz), material);
            } else {
                if (!material.empty() && material != "Default") {
                    ok = sub.chunkManager->m_voxelModificationSystem.addCubeWithMaterial(
                        glm::ivec3(x, y, z), material);
                } else {
                    ok = sub.chunkManager->addCube(glm::ivec3(x, y, z));
                }
            }
            if (ok) result.structuresPlaced++;
        } else {
            LOG_WARN("GameDefinitionLoader", "Unknown structure type: " + stype);
        }
    }
}

// ============================================================================
// Player
// ============================================================================

void GameDefinitionLoader::loadPlayer(const json& playerDef, GameSubsystems& sub, GameDefinitionResult& result) {
    if (!sub.entitySpawner) {
        LOG_WARN("GameDefinitionLoader", "Skipping player: no entity spawner configured");
        return;
    }

    std::string type = playerDef.value("type", "animated");
    float x = 16.0f, y = 20.0f, z = 16.0f;
    if (playerDef.contains("position")) {
        x = playerDef["position"].value("x", 16.0f);
        y = playerDef["position"].value("y", 20.0f);
        z = playerDef["position"].value("z", 16.0f);
    }
    // Race/appearance-aware players resolve through the shared visual path;
    // plain players keep the legacy default appearance + recolor.
    const bool hasVisualDef = playerDef.contains("race") || playerDef.contains("raceId") ||
                              playerDef.contains("appearance");
    std::string playerName = playerDef.value("id", "player");
    auto visual = CharacterVisualResolver::resolve(playerDef, playerName);

    Scene::Entity* entity = sub.entitySpawner(type, glm::vec3(x, y, z), visual.animFile);
    if (entity) {
        // FACTION for the player, same vocabulary as NPCs. Without this the
        // player is always UNALIGNED — which means hostile to everyone, so a
        // spectator standing near a battle gets cut down by both armies
        // (measured: the observer died mid-simulation while tagged "neutral"
        // in the game definition, because nothing read that tag).
        if (playerDef.contains("faction"))
            entity->setFaction(playerDef["faction"].get<std::string>());

        // Apply appearance to animated characters
        if (type == "animated") {
            auto* animChar = dynamic_cast<Scene::AnimatedVoxelCharacter*>(entity);
            if (animChar) {
                if (hasVisualDef) {
                    // Full rebuild so proportions apply too (recolor alone
                    // silently dropped them).
                    animChar->setAppearance(visual.appearance);
                    animChar->rebuildWithAppearance(visual.appearance);
                } else {
                    animChar->setAppearance(Scene::CharacterAppearance{});
                    animChar->recolorFromAppearance();
                }
            }
        }

        // Register with optional custom ID
        std::string id = playerDef.value("id", "player");
        if (sub.entityRegistry) {
            sub.entityRegistry->registerEntity(entity, id, type);
        }
        result.playerSpawned = true;
        LOG_INFO("GameDefinitionLoader", "Player spawned: " + type + " at (" +
                 std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + ")");
    } else {
        LOG_WARN("GameDefinitionLoader", "Failed to spawn player entity: " + type);
    }
}

// ============================================================================
// Camera
// ============================================================================

void GameDefinitionLoader::loadCamera(const json& cameraDef, GameSubsystems& sub, GameDefinitionResult& result) {
    if (!sub.camera) {
        LOG_WARN("GameDefinitionLoader", "Skipping camera: Camera not available");
        return;
    }

    if (cameraDef.contains("position")) {
        float x = cameraDef["position"].value("x", 50.0f);
        float y = cameraDef["position"].value("y", 50.0f);
        float z = cameraDef["position"].value("z", 50.0f);
        sub.camera->setPosition(glm::vec3(x, y, z));
    }

    if (cameraDef.contains("yaw")) {
        sub.camera->setYaw(cameraDef["yaw"].get<float>());
    }
    if (cameraDef.contains("pitch")) {
        sub.camera->setPitch(cameraDef["pitch"].get<float>());
    }

    // Optional camera mode — lets a game start first-person (e.g. a maze
    // crawler) without the player pressing V. Accepts snake_case and
    // PascalCase spellings. When present, hosts must NOT override it with
    // their own default (see result.cameraModeSet).
    if (cameraDef.contains("mode")) {
        const std::string mode = cameraDef["mode"].get<std::string>();
        result.cameraRig = mode;  // raw authored name; host resolves via Graphics::makeCameraRig
        if (mode == "first_person" || mode == "FirstPerson" || mode == "first") {
            sub.camera->setMode(Graphics::CameraMode::FirstPerson);
            result.cameraModeSet = true;
        } else if (mode == "third_person" || mode == "ThirdPerson" || mode == "third") {
            sub.camera->setMode(Graphics::CameraMode::ThirdPerson);
            result.cameraModeSet = true;
        } else if (mode == "free" || mode == "Free") {
            sub.camera->setMode(Graphics::CameraMode::Free);
            result.cameraModeSet = true;
        } else if (mode == "overhead" || mode == "Overhead" || mode == "top_down" ||
                   mode == "isometric" || mode == "Isometric" || mode == "iso") {
            // Orthographic camera rigs own positioning, so the CameraMode enum is
            // left as-is; the host resolves result.cameraRig via makeCameraRig().
            result.cameraModeSet = true;
        } else {
            LOG_WARN("GameDefinitionLoader", "Unknown camera mode '" + mode +
                     "' (expected first_person/third_person/free/overhead/isometric)");
            result.cameraRig.clear();
        }
    }

    // Optional control scheme (fps/tank/...); host resolves via Input::makeControlScheme.
    if (cameraDef.contains("controlScheme")) {
        result.controlScheme = cameraDef["controlScheme"].get<std::string>();
    }

    result.cameraSet = true;
}

// ============================================================================
// Locations
// ============================================================================

void GameDefinitionLoader::loadLocations(const json& locationsDef, GameSubsystems& sub, GameDefinitionResult& result) {
    if (!sub.locationRegistry) {
        LOG_WARN("GameDefinitionLoader", "Skipping locations: LocationRegistry not available");
        return;
    }

    for (const auto& locDef : locationsDef) {
        auto loc = Location::fromJson(locDef);
        if (loc.id.empty()) {
            LOG_WARN("GameDefinitionLoader", "Skipping location with empty ID");
            continue;
        }
        sub.locationRegistry->addLocation(loc);
        result.locationsRegistered++;
        LOG_DEBUG("GameDefinitionLoader", "Registered location '{}' ({}) at ({}, {}, {})",
                  loc.id, loc.name, loc.position.x, loc.position.y, loc.position.z);
    }
}

// ============================================================================
// NPCs
// ============================================================================

void GameDefinitionLoader::loadNPCs(const json& npcsDef, GameSubsystems& sub, GameDefinitionResult& result) {
    if (!sub.npcManager) {
        result.error = "NPCManager not available for NPC spawning";
        return;
    }

    for (const auto& npcDef : npcsDef) {
        std::string name = npcDef["name"].get<std::string>();

        float x = 0.0f, y = 20.0f, z = 0.0f;
        if (npcDef.contains("position")) {
            x = npcDef["position"].value("x", 0.0f);
            y = npcDef["position"].value("y", 20.0f);
            z = npcDef["position"].value("z", 0.0f);
        }

        std::string behaviorStr = npcDef.value("behavior", "idle");
        NPCBehaviorType behaviorType = NPCBehaviorType::Idle;
        std::vector<glm::vec3> waypoints;
        float walkSpeed = npcDef.value("walkSpeed", 2.0f);
        float waitTime = npcDef.value("waitTime", 2.0f);

        if (behaviorStr == "patrol") {
            behaviorType = NPCBehaviorType::Patrol;
            if (npcDef.contains("waypoints")) {
                for (const auto& wp : npcDef["waypoints"]) {
                    waypoints.emplace_back(wp.value("x", 0.0f), wp.value("y", 0.0f), wp.value("z", 0.0f));
                }
            }
        } else if (behaviorStr == "wander") {
            behaviorType = NPCBehaviorType::Wander;   // roam near spawn (fauna)
        } else if (behaviorStr == "follow") {
            behaviorType = NPCBehaviorType::Follow;   // party companion: follow the player
        } else if (behaviorStr == "behavior_tree") {
            behaviorType = NPCBehaviorType::BehaviorTree;
        } else if (behaviorStr == "scheduled") {
            behaviorType = NPCBehaviorType::Scheduled;
        } else if (behaviorStr == "combat") {
            behaviorType = NPCBehaviorType::Combat;        // real-time melee
        } else if (behaviorStr == "caster") {
            behaviorType = NPCBehaviorType::RangedCaster;  // real-time spellcaster
        }

        std::string npcRole = npcDef.value("role", "");

        // Resolve visual (race / preset / explicit appearance / animFile)
        // through the single shared path — see CharacterVisualResolver.
        auto visual = CharacterVisualResolver::resolve(npcDef, name);

        auto* npc = sub.npcManager->spawnNPC(name, visual.animFile, glm::vec3(x, y, z),
                                               behaviorType, waypoints, walkSpeed, waitTime,
                                               visual.appearance);
        if (!npc) {
            LOG_WARN("GameDefinitionLoader", "Failed to spawn NPC: " + name);
            continue;
        }
        result.npcsSpawned++;

        // FACTION + real-time combat loadout. "faction" picks the side (empty =
        // unaligned, hostile to everyone); it is published on the ENTITY so
        // other combatants can read it when choosing targets. Melee fighters
        // take a "weapon"; casters take "spells" plus range/cooldown tuning.
        const std::string faction = npcDef.value("faction", "");
        if (!faction.empty()) npc->setFaction(faction);

        // CHAIN OF COMMAND: "squad" puts this NPC under an officer's orders;
        // "rank":"officer" (or "leader") makes it the one giving them. Squads
        // are created on first mention so authoring order does not matter.
        if (sub.commandStructure && npcDef.contains("squad")) {
            const std::string squadId = npcDef["squad"].get<std::string>();
            const std::string rank = npcDef.value("rank", "");
            if (!sub.commandStructure->squad(squadId))
                sub.commandStructure->createSquad(squadId, faction);
            const bool isLeader = (rank == "officer" || rank == "leader" ||
                                   rank == "sergeant" || rank == "captain");
            sub.commandStructure->addMember(squadId, "npc_" + name, isLeader);
            // The RALLY point is where the squad formed up — a running mean of
            // its members' spawn positions. Without this it stays (0,0,0) and a
            // "fall back" order marches the squad to the corner of the world.
            if (auto* sq = sub.commandStructure->squad(squadId)) {
                const float n = static_cast<float>(sq->members.size());
                sq->rally += (glm::vec3(x, y, z) - sq->rally) / n;
            }
        }
        if (behaviorType == NPCBehaviorType::Combat) {
            if (auto* cb = dynamic_cast<Scene::CombatBehavior*>(npc->getBehavior())) {
                if (!faction.empty()) cb->setFaction(faction);
                if (npcDef.contains("weapon")) cb->setWeapon(npcDef["weapon"].get<std::string>());
                // TACTICAL: "intelligence" (3-18) drives reaction speed, cover
                // discipline, obedience and target choice; the chunk manager
                // gives it line-of-sight so it can find cover at all.
                if (npcDef.contains("intelligence"))
                    cb->setIntelligence(npcDef["intelligence"].get<int>());
                cb->setChunkManager(sub.chunkManager);
                // Shared async pathfinder: without it approach is a straight
                // line and any wall stops the fighter dead against its face.
                if (sub.npcManager) cb->setPathService(sub.npcManager->getPathService());
                if (sub.commandStructure) cb->setCommandStructure(sub.commandStructure);
                if (npcDef.contains("aggro_range"))    cb->setAggroRange(npcDef["aggro_range"].get<float>());
                if (npcDef.contains("attack_damage"))  cb->setAttackDamage(npcDef["attack_damage"].get<float>());
                if (npcDef.contains("attack_cooldown"))cb->setAttackCooldown(npcDef["attack_cooldown"].get<float>());
            }
        } else if (behaviorType == NPCBehaviorType::RangedCaster) {
            if (auto* rb = dynamic_cast<Scene::RangedCasterBehavior*>(npc->getBehavior())) {
                if (!faction.empty()) rb->setFaction(faction);
                // Line of sight: without the world, a caster fires through walls.
                rb->setChunkManager(sub.chunkManager);
                if (npcDef.contains("spells") && npcDef["spells"].is_array()) {
                    std::vector<std::string> spells;
                    for (const auto& s : npcDef["spells"]) spells.push_back(s.get<std::string>());
                    rb->setSpells(std::move(spells));
                }
                if (npcDef.contains("aggro_range"))     rb->setAggroRange(npcDef["aggro_range"].get<float>());
                if (npcDef.contains("preferred_range")) rb->setPreferredRange(npcDef["preferred_range"].get<float>());
                if (npcDef.contains("cast_cooldown"))   rb->setCastCooldown(npcDef["cast_cooldown"].get<float>());
                if (npcDef.contains("spell_damage"))    rb->setDamage(npcDef["spell_damage"].get<float>());
            }
        }

        // Race/definition gait flavor: FSM state -> clip overrides (halfling
        // scamper, ogre prowl) resolved by CharacterVisualResolver.
        if (!visual.animationMapping.empty()) {
            if (auto* ch = npc->getAnimatedCharacter()) {
                for (const auto& [state, clip] : visual.animationMapping)
                    ch->setAnimationMapping(state, clip);
            }
        }

        // BEHAVIOR TREE NPCs — "behavior":"behavior_tree" + "behaviorTree":"<file>".
        //
        // This used to be handled ONLY inside the Scheduled branch below, so an NPC declared as
        // "behavior_tree" was given a BehaviorTreeBehavior and then never given its TREE: it stood
        // still forever, with no error anywhere. A 144-combatant siege authored entirely in JSON
        // loaded, spawned every NPC, and did not throw a single punch in 78 s because of this.
        // Loading it here is what makes "author new AI as data" actually true.
        if (behaviorType == NPCBehaviorType::BehaviorTree) {
            auto* btb = dynamic_cast<Scene::BehaviorTreeBehavior*>(npc->getBehavior());
            if (!btb) {
                LOG_WARN("GameDefinitionLoader",
                         "NPC {}: behavior_tree requested but the behavior is not a "
                         "BehaviorTreeBehavior — it will do nothing", name);
            } else if (!npcDef.contains("behaviorTree")) {
                LOG_WARN("GameDefinitionLoader",
                         "NPC {}: behavior 'behavior_tree' with no \"behaviorTree\" file — it "
                         "will stand still", name);
            } else {
                const std::string btFile = npcDef["behaviorTree"].get<std::string>();
                auto btRoot = AI::BTLoader::fromFile(btFile);
                if (!btRoot) {
                    // Loudly, and at INFO/WARN so it survives a Release build: a silently
                    // brainless NPC is indistinguishable from a broken behaviour.
                    LOG_WARN("GameDefinitionLoader",
                             "NPC {}: could not load behavior tree '{}' (missing file or bad "
                             "JSON) — it will stand still", name, btFile);
                } else {
                    btb->setTree(std::move(btRoot));
                    LOG_INFO("GameDefinitionLoader", "NPC {}: behavior tree '{}' attached",
                             name, btFile);
                }
            }
        }

        // Configure schedule if this is a scheduled NPC
        if (behaviorType == NPCBehaviorType::Scheduled) {
            auto* sb = dynamic_cast<Scene::ScheduledBehavior*>(npc->getBehavior());
            if (sb) {
                // Use explicit schedule from JSON, or role-based default
                if (npcDef.contains("schedule")) {
                    sb->setSchedule(AI::Schedule::fromJson(npcDef["schedule"]));
                } else if (!npcRole.empty()) {
                    sb->setSchedule(AI::Schedule::forRole(npcRole));
                }

                // Load behavior tree from JSON file if specified
                if (npcDef.contains("behaviorTree")) {
                    std::string btFile = npcDef["behaviorTree"].get<std::string>();
                    auto btRoot = AI::BTLoader::fromFile(btFile);
                    if (btRoot) {
                        sb->setTree(std::move(btRoot));
                        LOG_DEBUG("GameDefinitionLoader", "NPC " + name + ": loaded BT from " + btFile);
                    }
                }
            }
        }

        // Configure health if provided
        if (npcDef.contains("health") || npcDef.contains("maxHealth")) {
            auto* health = npc->getHealthComponent();
            if (health) {
                if (npcDef.contains("maxHealth")) {
                    health->setMaxHealth(npcDef["maxHealth"].get<float>());
                    health->setHealth(npcDef["maxHealth"].get<float>());
                }
                if (npcDef.contains("health")) {
                    health->setHealth(npcDef["health"].get<float>());
                }
                if (npcDef.contains("invulnerable")) {
                    health->setInvulnerable(npcDef["invulnerable"].get<bool>());
                }
            }
        }

        // Set up dialogue if provided
        if (npcDef.contains("dialogue") && sub.dialogueSystem) {
            int agencyLevel = 0;
            if (npcDef.contains("storyCharacter")) {
                agencyLevel = npcDef["storyCharacter"].value("agencyLevel", 0);
            }

            if (agencyLevel >= 1) {
                // Hybrid dialogue: parse tree AND enable AI enhancement
                auto tree = UI::DialogueTree::fromJson(npcDef["dialogue"]);
                auto treePtr = std::make_unique<UI::DialogueTree>(std::move(tree));

                std::string entityId = "";
                if (npcDef.contains("storyCharacter")) {
                    entityId = npcDef["storyCharacter"].value("id", name);
                }
                if (entityId.empty()) entityId = name;

                auto aiProvider = std::make_unique<UI::AIDialogueProvider>(entityId, name, std::move(treePtr));
                npc->setDialogueProvider(std::move(aiProvider));

                // Also register with AI system via callback
                if (sub.aiRegister) {
                    std::string personality = npcDef.value("personality", "");
                    sub.aiRegister(npc, entityId, name, personality);
                }
                LOG_DEBUG("GameDefinitionLoader", "NPC " + name + ": hybrid dialogue configured (tree + AI, agencyLevel=" + std::to_string(agencyLevel) + ")");
            } else {
                // Static dialogue tree
                auto tree = UI::DialogueTree::fromJson(npcDef["dialogue"]);
                auto provider = std::make_unique<UI::StaticDialogueProvider>(std::move(tree));
                npc->setDialogueProvider(std::move(provider));
                LOG_DEBUG("GameDefinitionLoader", "NPC " + name + ": static dialogue configured");
            }
        } else if (npcDef.contains("storyCharacter")) {
            // No dialogue provided but has storyCharacter — check if AI mode
            int agencyLevel = npcDef["storyCharacter"].value("agencyLevel", 0);
            if (agencyLevel >= 1) {
                std::string entityId = npcDef["storyCharacter"].value("id", name);
                if (entityId.empty()) entityId = name;

                auto aiProvider = std::make_unique<UI::AIDialogueProvider>(entityId, name);
                npc->setDialogueProvider(std::move(aiProvider));

                if (sub.aiRegister) {
                    std::string personality = npcDef.value("personality", "");
                    sub.aiRegister(npc, entityId, name, personality);
                }
                LOG_DEBUG("GameDefinitionLoader", "NPC " + name + ": AI dialogue configured (no static tree, agencyLevel=" + std::to_string(agencyLevel) + ")");
            }
        }

        // Register story character if provided. Parse the whole block through
        // CharacterProfile::from_json so the full rich profile (backstory, drives,
        // likes/dislikes/prejudices, speechStyle, voice, emotion, relationships, ...)
        // flows through — not just the handful of fields we used to copy by hand.
        if (npcDef.contains("storyCharacter") && sub.storyEngine) {
            // Work on a normalized copy; leave npcDef untouched so the integer-agencyLevel
            // reads above (dialogue wiring) keep working.
            nlohmann::json sc = npcDef["storyCharacter"];
            if (!sc.contains("id"))        sc["id"]   = name;          // profile id == NPC name (shared key)
            if (!sc.contains("name"))      sc["name"] = name;
            if (sc.contains("faction") && !sc.contains("factionId"))   // legacy field name
                sc["factionId"] = sc["faction"];
            if (sc.contains("agencyLevel") && sc["agencyLevel"].is_number())  // legacy int form
                sc["agencyLevel"] = Story::agencyLevelToString(
                    static_cast<Story::AgencyLevel>(sc["agencyLevel"].get<int>()));

            try {
                Story::CharacterProfile profile = sc.get<Story::CharacterProfile>();
                const std::string charId = profile.id;
                const Story::AgencyLevel agency = profile.agencyLevel;
                sub.storyEngine->addCharacter(std::move(profile));
                LOG_DEBUG("GameDefinitionLoader", "NPC " + name + ": story character registered (full profile)");

                // Guided/Autonomous characters get a profile-driven behavior so they act on
                // their personality/goals (wander, idle, ambient chatter) via the shared agent.
                if (npc && sub.characterAgent && agency >= Story::AgencyLevel::Guided) {
                    Story::CharacterProfile* prof = sub.storyEngine->getCharacterMut(charId);
                    Story::CharacterMemory*  mem  = sub.storyEngine->getCharacterMemoryMut(charId);
                    if (prof) {
                        auto sdb = std::make_unique<Scene::StoryDrivenBehavior>(
                            sub.characterAgent, prof, mem, sub.storyEngine);

                        // Daily routine: explicit schedule from JSON, else a role-based default
                        // (merchant/guard/farmer/innkeeper). Gives the autonomy a purpose —
                        // the character heads to scheduled locations by time of day.
                        if (npcDef.contains("schedule")) {
                            sdb->setSchedule(AI::Schedule::fromJson(npcDef["schedule"]));
                        } else {
                            std::string role = !npcRole.empty() ? npcRole
                                             : (!prof->roles.empty() ? prof->roles.front() : "");
                            if (!role.empty()) sdb->setSchedule(AI::Schedule::forRole(role));
                        }

                        npc->setBehavior(std::move(sdb));
                        LOG_INFO("GameDefinitionLoader",
                                 "NPC " + name + ": StoryDrivenBehavior attached (agency>=Guided)");
                    }
                }
            } catch (const std::exception& e) {
                // Covers profile parse, story registration, AND schedule parse below —
                // keep the message general so the schedule isn't misattributed to the profile.
                LOG_WARN("GameDefinitionLoader", "NPC " + name + ": failed to set up story character/behavior: " + e.what());
            }
        }

        // Load per-NPC needs if provided (overrides defaults)
        if (npcDef.contains("needs")) {
            npc->getNeeds().fromJson(npcDef["needs"]);
        }

        // Load per-NPC worldview if provided
        if (npcDef.contains("worldView")) {
            npc->getWorldView().fromJson(npcDef["worldView"]);
        }

        // Load initial relationships (stored on the shared RelationshipManager)
        if (npcDef.contains("relationships")) {
            for (const auto& relDef : npcDef["relationships"]) {
                std::string targetId = relDef.value("target", "");
                if (targetId.empty()) continue;
                Story::Relationship rel;
                rel.targetCharacterId = targetId;
                rel.trust = relDef.value("trust", 0.0f);
                rel.affection = relDef.value("affection", 0.0f);
                rel.respect = relDef.value("respect", 0.0f);
                rel.fear = relDef.value("fear", 0.0f);
                rel.label = relDef.value("label", "");
                sub.npcManager->getRelationships().setRelationship(name, targetId, rel);
            }
        }

        if (sub.gameEventLog) {
            sub.gameEventLog->emit("npc_spawned", {
                {"name", name}, {"source", "game_definition"},
                {"position", {{"x", x}, {"y", y}, {"z", z}}}
            });
        }
    }
}

// ============================================================================
// Story
// ============================================================================

void GameDefinitionLoader::loadStory(const json& storyDef, GameSubsystems& sub, GameDefinitionResult& result) {
    if (!sub.storyEngine) {
        LOG_WARN("GameDefinitionLoader", "Skipping story: StoryEngine not available");
        return;
    }

    // Use StoryWorldLoader for the world section (factions, locations, variables)
    if (storyDef.contains("world")) {
        std::string err;
        if (!Story::StoryWorldLoader::loadFromJson(storyDef["world"], *sub.storyEngine, &err)) {
            result.error = "Failed to load story world: " + err;
            return;
        }
    }

    // Load story arcs
    if (storyDef.contains("arcs")) {
        for (const auto& arcDef : storyDef["arcs"]) {
            Story::StoryArc arc;
            arc.id = arcDef.value("id", "");
            arc.name = arcDef.value("name", "");
            arc.description = arcDef.value("description", "");

            std::string modeStr = arcDef.value("constraintMode", "Guided");
            if (modeStr == "Scripted") arc.constraintMode = Story::ArcConstraintMode::Scripted;
            else if (modeStr == "Guided") arc.constraintMode = Story::ArcConstraintMode::Guided;
            else if (modeStr == "Emergent") arc.constraintMode = Story::ArcConstraintMode::Emergent;
            else if (modeStr == "Freeform") arc.constraintMode = Story::ArcConstraintMode::Freeform;

            if (arcDef.contains("beats")) {
                for (const auto& beatDef : arcDef["beats"]) {
                    Story::StoryBeat beat;
                    beat.id = beatDef.value("id", "");
                    beat.description = beatDef.value("description", "");

                    std::string beatTypeStr = beatDef.value("type", "Soft");
                    if (beatTypeStr == "Hard") beat.type = Story::BeatType::Hard;
                    else if (beatTypeStr == "Soft") beat.type = Story::BeatType::Soft;
                    else if (beatTypeStr == "Optional") beat.type = Story::BeatType::Optional;

                    beat.triggerCondition = beatDef.value("triggerCondition", "");
                    beat.completionCondition = beatDef.value("completionCondition", "");
                    beat.failureCondition = beatDef.value("failureCondition", "");

                    if (beatDef.contains("requiredCharacters")) {
                        for (const auto& rc : beatDef["requiredCharacters"]) {
                            beat.requiredCharacters.push_back(rc.get<std::string>());
                        }
                    }
                    if (beatDef.contains("prerequisites")) {
                        for (const auto& pr : beatDef["prerequisites"]) {
                            beat.prerequisites.push_back(pr.get<std::string>());
                        }
                    }

                    arc.beats.push_back(std::move(beat));
                }
            }

            if (arcDef.contains("tensionCurve")) {
                for (const auto& t : arcDef["tensionCurve"]) {
                    arc.tensionCurve.push_back(t.get<float>());
                }
            }

            sub.storyEngine->addStoryArc(std::move(arc), true);
        }
    }

    result.storyLoaded = true;
    LOG_INFO("GameDefinitionLoader", "Story loaded");
}

// ============================================================================
// Export
// ============================================================================

json GameDefinitionLoader::exportDefinition(const GameSubsystems& subsystems) {
    json def;
    def["name"] = "Exported Game";
    def["version"] = "1.0";

    // Export camera
    if (subsystems.camera) {
        auto pos = subsystems.camera->getPosition();
        def["camera"] = {
            {"position", {{"x", pos.x}, {"y", pos.y}, {"z", pos.z}}},
            {"yaw", subsystems.camera->getYaw()},
            {"pitch", subsystems.camera->getPitch()}
        };
    }

    // Export NPCs
    if (subsystems.npcManager) {
        json npcsJson = json::array();
        auto npcNames = subsystems.npcManager->getAllNPCNames();
        for (const auto& name : npcNames) {
            auto* npc = subsystems.npcManager->getNPC(name);
            if (!npc) continue;
            json npcJson;
            npcJson["name"] = name;
            auto npcPos = npc->getPosition();
            npcJson["position"] = {{"x", npcPos.x}, {"y", npcPos.y}, {"z", npcPos.z}};
            npcsJson.push_back(std::move(npcJson));
        }
        if (!npcsJson.empty()) {
            def["npcs"] = std::move(npcsJson);
        }
    }

    // Export story state
    if (subsystems.storyEngine) {
        def["story"] = {{"state", subsystems.storyEngine->saveState()}};
    }

    return def;
}

// ============================================================================
// Multi-Scene Support
// ============================================================================

json GameDefinitionLoader::exportMultiSceneDefinition(
    const SceneManifest& manifest,
    const std::string& activeSceneId,
    const GameSubsystems& subsystems)
{
    // Start from the manifest's own serialization
    json def = manifest.toJson();
    def["name"] = def.value("name", "Exported Game");
    def["version"] = "1.0";

    // Patch the active scene's definition with live camera/NPC state
    if (!activeSceneId.empty()) {
        json liveState = exportDefinition(subsystems);
        for (auto& sceneDef : def["scenes"]) {
            if (sceneDef.value("id", "") == activeSceneId) {
                auto& d = sceneDef["definition"];
                if (liveState.contains("camera")) d["camera"] = liveState["camera"];
                if (liveState.contains("npcs"))   d["npcs"]   = liveState["npcs"];
                if (liveState.contains("story"))  d["story"]  = liveState["story"];
                break;
            }
        }
    }

    return def;
}

SceneManifest GameDefinitionLoader::parseManifest(const json& definition) {
    if (!SceneManifest::isMultiScene(definition)) {
        return {};
    }
    return SceneManifest::fromJson(definition);
}

} // namespace Core
} // namespace Phyxel
