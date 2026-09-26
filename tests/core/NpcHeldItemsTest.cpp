// NpcHeldItems: what an NPC holds is drawn at its hand, and a held torch's flame light rides
// the hand (torch feature, increment 1, 2026-09-26). Headless: real NPCManager + humanoid rig,
// real ItemRegistry (resources/items.json), real ItemEffectSystem with a recording light sink.
#include <gtest/gtest.h>

#include "core/ChunkManager.h"
#include "core/ItemEffectSystem.h"
#include "core/ItemRegistry.h"
#include "core/KinematicVoxelManager.h"
#include "core/NPCManager.h"
#include "core/NpcHeldItems.h"
#include "core/ObjectTemplateManager.h"
#include "physics/PhysicsWorld.h"
#include "scene/NPCEntity.h"

#include <cstdio>
#include <map>

using namespace Phyxel;
using namespace Phyxel::Core;

namespace {
struct Rig {
    ChunkManager cm;
    Phyxel::Physics::PhysicsWorld physics;
    NPCManager npcs;
    KinematicVoxelManager kin;
    ObjectTemplateManager templates{nullptr, nullptr};
    ItemEffectSystem effects;
    std::map<int, glm::vec3> lights;   // live point lights by id
    int nextLight = 1;
    std::unique_ptr<NpcHeldItems> held;

    Rig() {
        cm.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);
        auto chunk = std::make_unique<Phyxel::Chunk>(glm::ivec3(0, 0, 0));
        chunk->initializeForLoading();
        for (int x = 0; x < 32; ++x)
            for (int z = 0; z < 32; ++z) chunk->addCube(glm::ivec3(x, 15, z));
        cm.chunkMap[glm::ivec3(0, 0, 0)] = chunk.get();
        cm.chunks.push_back(std::move(chunk));
        physics.initialize();
        npcs.setChunkManager(&cm);
        npcs.setPhysicsWorld(&physics);
        ItemRegistry::instance().loadFromFile("resources/items.json");
        effects.setLightCallbacks(
            [this](const glm::vec3& p, const glm::vec3&, float, float) { lights[nextLight] = p; return nextLight++; },
            [this](int id, const glm::vec3& p) { lights[id] = p; },
            [this](int id) { lights.erase(id); });
        ObjectTemplateManager* t = &templates;
        held = std::make_unique<NpcHeldItems>(&kin, [t](const ItemDefinition& d) -> const VoxelTemplate* {
            if (const VoxelTemplate* v = t->getTemplate(d.id)) return v;
            t->loadTemplate("resources/templates/" + d.templateFile, d.id);
            return t->getTemplate(d.id);
        }, &effects);
    }
    Scene::NPCEntity* spawn(const std::string& name, glm::vec3 at) {
        return npcs.spawnNPC(name, "resources/animated_characters/humanoid.anim", at, NPCBehaviorType::Idle);
    }
    void tick(int frames = 3) {
        for (int i = 0; i < frames; ++i) {
            npcs.update(1.0f / 30.0f);
            held->update(npcs);
            effects.update(1.0f / 30.0f);
        }
    }
};
}  // namespace

// The contract: a townsperson given a torch holds it at the right hand, and the torch's flame
// light sits at the torch's flame anchor - within half a unit of the hand, not at the body or the
// world origin. Taking the torch away removes the mesh AND the light.
TEST(NpcHeldItemsTest, ACarriedTorchIsDrawnAtTheHandAndItsLightRidesIt) {
    Rig r;
    Scene::NPCEntity* ada = r.spawn("Ada", glm::vec3(16.5f, 16.0f, 16.5f));
    ASSERT_NE(ada, nullptr) << "run from the repo root";
    r.tick();
    EXPECT_EQ(r.held->heldItem("Ada"), "") << "empty hands by default";
    EXPECT_TRUE(r.lights.empty());

    ada->setCarriedItem("torch");
    r.tick();
    ASSERT_EQ(r.held->heldItem("Ada"), "torch");
    ASSERT_EQ(r.lights.size(), 1u) << "the torch's flame must light: 0 lights means its effects were never registered";
    const glm::vec3 light = r.lights.begin()->second;
    const glm::mat4 mesh = r.kin.getTransform(r.held->heldMeshId("Ada"));
    const glm::vec3 handGrip(mesh[3]);
    EXPECT_LT(glm::length(light - handGrip), 0.5f) << "the light must sit on the torch, not at the body";
    const glm::vec3 body = ada->getPosition();
    EXPECT_GT(light.y, body.y + 0.3f) << "a hand-held flame is above the feet";
    EXPECT_LT(glm::length(glm::vec2(light.x - body.x, light.z - body.z)), 1.2f) << "and beside the body";
    // The flame is at the torch's HEAD: it must stand above the hand, or the torch is carried
    // upside down (a burning head below the fist).
    const glm::vec3 shaftUp = glm::normalize(glm::vec3(mesh * glm::vec4(0, 1, 0, 0)));
    std::printf("[held] grip=(%.3f,%.3f,%.3f) light=(%.3f,%.3f,%.3f) body=(%.3f,%.3f,%.3f) shaftUp=(%.2f,%.2f,%.2f)\n",
                handGrip.x, handGrip.y, handGrip.z, light.x, light.y, light.z, body.x, body.y, body.z,
                shaftUp.x, shaftUp.y, shaftUp.z);
    const glm::vec3 handle(mesh * glm::vec4(0.0185f, 0.1049f, 0.0185f, 1.0f));
    std::printf("[held] handle=(%.4f,%.4f,%.4f)\n", handle.x, handle.y, handle.z);
    EXPECT_GT(light.y, handGrip.y + 0.15f) << "the flame hangs below the hand: the torch is upside down";
    EXPECT_GT(shaftUp.y, 0.7f) << "an idle torch-bearer holds the shaft roughly upright";

    ada->setCarriedItem("");
    r.tick();
    EXPECT_EQ(r.held->heldItem("Ada"), "");
    EXPECT_TRUE(r.lights.empty()) << "putting the torch away must remove its light";
}

// An unknown item draws nothing, and a despawned
// NPC takes its item and its light with it.
TEST(NpcHeldItemsTest, UnknownItemsDrawNothingAndADespawnTakesTheLight) {
    Rig r;
    Scene::NPCEntity* bram = r.spawn("Bram", glm::vec3(10.5f, 16.0f, 10.5f));
    ASSERT_NE(bram, nullptr);
    bram->setCarriedItem("no_such_item");
    r.tick();
    EXPECT_EQ(r.held->heldItem("Bram"), "");
    bram->setCarriedItem("torch");
    r.tick();
    ASSERT_EQ(r.held->heldItem("Bram"), "torch");
    ASSERT_EQ(r.lights.size(), 1u);
    r.npcs.removeNPC("Bram");
    r.tick();
    EXPECT_EQ(r.held->count(), 0u);
    EXPECT_TRUE(r.lights.empty()) << "a despawned NPC's torch light must go with it";
}
