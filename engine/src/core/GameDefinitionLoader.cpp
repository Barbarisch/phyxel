#include "core/GameDefinitionLoader.h"
#include "core/ChunkManager.h"
#include "core/WorldGenerator.h"
#include "core/StructureGenerator.h"
#include "core/NPCManager.h"
#include "core/EntityRegistry.h"
#include "core/ObjectTemplateManager.h"
#include "core/GameEventLog.h"
#include "core/HealthComponent.h"
#include "core/LocationRegistry.h"
#include "scene/NPCEntity.h"
#include "scene/AnimatedVoxelCharacter.h"
#include "scene/behaviors/ScheduledBehavior.h"
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

    // Validate structures
    if (definition.contains("structures")) {
        if (!definition["structures"].is_array()) return {false, "'structures' must be an array"};
        for (size_t i = 0; i < definition["structures"].size(); i++) {
            const auto& s = definition["structures"][i];
            if (!s.contains("type") || !s["type"].is_string()) {
                return {false, "Structure at index " + std::to_string(i) + " missing 'type'"};
            }
            std::string stype = s["type"].get<std::string>();
            if (stype != "fill" && stype != "template") {
                return {false, "Invalid structure type '" + stype + "' at index " + std::to_string(i)};
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
        loadWorld(definition["world"], subsystems, result);
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

void GameDefinitionLoader::loadWorld(const json& worldDef, GameSubsystems& sub, GameDefinitionResult& result) {
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

            int minX = std::min(x1, x2), maxX = std::max(x1, x2);
            int minY = std::min(y1, y2), maxY = std::max(y1, y2);
            int minZ = std::min(z1, z2), maxZ = std::max(z1, z2);

            int64_t volume = (int64_t)(maxX - minX + 1) * (maxY - minY + 1) * (maxZ - minZ + 1);
            if (volume > 100000) {
                LOG_WARN("GameDefinitionLoader", "Skipping fill structure: volume " +
                         std::to_string(volume) + " exceeds 100k limit");
                continue;
            }

            int placed = 0;
            for (int ix = minX; ix <= maxX; ++ix) {
                for (int iy = minY; iy <= maxY; ++iy) {
                    for (int iz = minZ; iz <= maxZ; ++iz) {
                        if (hollow && ix > minX && ix < maxX &&
                            iy > minY && iy < maxY && iz > minZ && iz < maxZ) {
                            continue;
                        }
                        bool ok = false;
                        if (!material.empty()) {
                            ok = sub.chunkManager->m_voxelModificationSystem.addCubeWithMaterial(
                                glm::ivec3(ix, iy, iz), material);
                        } else {
                            ok = sub.chunkManager->addCube(glm::ivec3(ix, iy, iz));
                        }
                        if (ok) placed++;
                    }
                }
            }
            result.structuresPlaced++;
            LOG_DEBUG("GameDefinitionLoader", "Fill: placed " + std::to_string(placed) + " voxels");

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

            if (sub.templateManager->spawnTemplate(templateName, glm::vec3(x, y, z), isStatic)) {
                result.structuresPlaced++;
                LOG_DEBUG("GameDefinitionLoader", "Template: spawned " + templateName);
            } else {
                LOG_WARN("GameDefinitionLoader", "Failed to spawn template: " + templateName);
            }

        } else if (stype == "house" || stype == "tavern" || stype == "tower" ||
                   stype == "wall" || stype == "room" || stype == "box" ||
                   stype == "staircase" || stype == "table" || stype == "chair" ||
                   stype == "counter" || stype == "bed") {
            // Procedural structure types — delegate to StructureGenerator
            auto structure = StructureGenerator::generateFromJson(s);
            auto placement = StructureGenerator::place(sub.chunkManager, structure);

            if (placement.placed > 0) {
                result.structuresPlaced++;
                LOG_DEBUG("GameDefinitionLoader", stype + ": placed " + std::to_string(placement.placed) +
                          " voxels (" + std::to_string(placement.failed) + " failed)");
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
    std::string animFile = playerDef.value("animFile", "resources/animated_characters/humanoid.anim");

    Scene::Entity* entity = sub.entitySpawner(type, glm::vec3(x, y, z), animFile);
    if (entity) {
        // Apply appearance to animated characters
        if (type == "animated") {
            auto* animChar = dynamic_cast<Scene::AnimatedVoxelCharacter*>(entity);
            if (animChar) {
                Scene::CharacterAppearance appearance;
                if (playerDef.contains("appearance")) {
                    appearance = Scene::CharacterAppearance::fromJson(playerDef["appearance"]);
                }
                animChar->setAppearance(appearance);
                animChar->recolorFromAppearance();
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
        std::string animFile = npcDef.value("animFile", "resources/animated_characters/humanoid.anim");

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
        } else if (behaviorStr == "behavior_tree") {
            behaviorType = NPCBehaviorType::BehaviorTree;
        } else if (behaviorStr == "scheduled") {
            behaviorType = NPCBehaviorType::Scheduled;
        }

        // Parse or generate appearance
        std::string npcRole = npcDef.value("role", "");
        Scene::CharacterAppearance appearance;
        if (npcDef.contains("appearance")) {
            appearance = Scene::CharacterAppearance::fromJson(npcDef["appearance"]);
        } else {
            // Detect morphology from anim file name
            std::string animLower = animFile;
            std::transform(animLower.begin(), animLower.end(), animLower.begin(), ::tolower);
            Scene::MorphologyType morph = Scene::MorphologyType::Humanoid;
            if (animLower.find("wolf") != std::string::npos)
                morph = Scene::MorphologyType::Quadruped;
            else if (animLower.find("spider") != std::string::npos)
                morph = Scene::MorphologyType::Arachnid;
            else if (animLower.find("dragon") != std::string::npos)
                morph = Scene::MorphologyType::Dragon;
            appearance = Scene::CharacterAppearance::generateFromSeed(name, npcRole, morph);
        }

        auto* npc = sub.npcManager->spawnNPC(name, animFile, glm::vec3(x, y, z),
                                               behaviorType, waypoints, walkSpeed, waitTime,
                                               appearance);
        if (!npc) {
            LOG_WARN("GameDefinitionLoader", "Failed to spawn NPC: " + name);
            continue;
        }
        result.npcsSpawned++;

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

        // Register story character if provided
        if (npcDef.contains("storyCharacter") && sub.storyEngine) {
            const auto& sc = npcDef["storyCharacter"];
            Story::CharacterProfile profile;
            profile.id = sc.value("id", name);
            profile.name = sc.value("name", name);
            profile.description = sc.value("description", "");
            profile.factionId = sc.value("faction", "");
            profile.agencyLevel = static_cast<Story::AgencyLevel>(sc.value("agencyLevel", 1));

            if (sc.contains("traits")) {
                const auto& t = sc["traits"];
                profile.traits.openness = t.value("openness", 0.5f);
                profile.traits.conscientiousness = t.value("conscientiousness", 0.5f);
                profile.traits.extraversion = t.value("extraversion", 0.5f);
                profile.traits.agreeableness = t.value("agreeableness", 0.5f);
                profile.traits.neuroticism = t.value("neuroticism", 0.5f);
            }

            if (sc.contains("goals")) {
                for (const auto& g : sc["goals"]) {
                    Story::CharacterGoal goal;
                    goal.id = g.value("id", "");
                    goal.description = g.value("description", "");
                    goal.priority = g.value("priority", 0.5f);
                    goal.isActive = g.value("isActive", true);
                    profile.goals.push_back(std::move(goal));
                }
            }

            if (sc.contains("roles")) {
                for (const auto& r : sc["roles"]) {
                    profile.roles.push_back(r.get<std::string>());
                }
            }

            sub.storyEngine->addCharacter(std::move(profile));
            LOG_DEBUG("GameDefinitionLoader", "NPC " + name + ": story character registered");
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

} // namespace Core
} // namespace Phyxel
