// NpcTorchPolicy: townsfolk light a torch outdoors at night, and "dark" NPCs in enclosed places
// (Ravenmere T-73, 2026-09-26). Pure policy tables, the two hysteresis latches, the world-state
// shelter scan (incl. the chunk-border equality test the design gate requires), and an
// integration run: real NPCManager + DayNightCycle + NpcHeldItems + ItemEffectSystem.
#include <gtest/gtest.h>

#include "core/ChunkManager.h"
#include "core/ItemEffectSystem.h"
#include "core/ItemRegistry.h"
#include "core/KinematicVoxelManager.h"
#include "core/NPCManager.h"
#include "core/NpcHeldItems.h"
#include "core/NpcTorchPolicy.h"
#include "core/ObjectTemplateManager.h"
#include "graphics/DayNightCycle.h"
#include "physics/PhysicsWorld.h"
#include "scene/NPCEntity.h"

#include <map>
#include <set>

using namespace Phyxel;
using namespace Phyxel::Core;

// ---------------------------------------------------------------- pure policy

TEST(NpcTorchPolicyTest, PolicyTableMatchesTheContract) {
    struct Row { TorchPolicy p; bool night, indoors, want; };
    const Row rows[] = {
        {TorchPolicy::Night,  false, false, false}, {TorchPolicy::Night,  true, false, true},
        {TorchPolicy::Night,  true,  true,  false}, {TorchPolicy::Night,  false, true, false},
        {TorchPolicy::Dark,   false, false, false}, {TorchPolicy::Dark,   true, false, true},
        {TorchPolicy::Dark,   true,  true,  true},  {TorchPolicy::Dark,   false, true, true},
        {TorchPolicy::Always, false, false, true},  {TorchPolicy::Never,  true, false, false},
    };
    for (const Row& r : rows)
        EXPECT_EQ(NpcTorchPolicy::wantsTorch(r.p, r.night, r.indoors), r.want)
            << NpcTorchPolicy::name(r.p) << " night=" << r.night << " indoors=" << r.indoors;

    // Defaults: townsfolk -> night (or the game default); hostiles and monsters -> never;
    // an NPC's own policy always wins.
    EXPECT_EQ(NpcTorchPolicy::resolve(TorchPolicy::Default, TorchPolicy::Default, false, false), TorchPolicy::Night);
    EXPECT_EQ(NpcTorchPolicy::resolve(TorchPolicy::Default, TorchPolicy::Dark, false, false), TorchPolicy::Dark);
    EXPECT_EQ(NpcTorchPolicy::resolve(TorchPolicy::Default, TorchPolicy::Default, true, false), TorchPolicy::Never);
    EXPECT_EQ(NpcTorchPolicy::resolve(TorchPolicy::Default, TorchPolicy::Always, false, true), TorchPolicy::Never);
    EXPECT_EQ(NpcTorchPolicy::resolve(TorchPolicy::Always, TorchPolicy::Default, true, false), TorchPolicy::Always);

    TorchPolicy p;
    EXPECT_TRUE(NpcTorchPolicy::parse("dark", p));
    EXPECT_EQ(p, TorchPolicy::Dark);
    EXPECT_FALSE(NpcTorchPolicy::parse("Night", p)) << "values are case-sensitive, like material names";
    EXPECT_FALSE(NpcTorchPolicy::parse("sometimes", p));
}

TEST(NpcTorchPolicyTest, NightAndIndoorLatchesHaveHysteresis) {
    // Night: on below -2 deg, off above +2 deg, unchanged inside the band.
    EXPECT_FALSE(NpcTorchPolicy::updateNight(false, 0.0f));
    EXPECT_FALSE(NpcTorchPolicy::updateNight(false, -1.9f));
    EXPECT_TRUE(NpcTorchPolicy::updateNight(false, -2.1f));
    EXPECT_TRUE(NpcTorchPolicy::updateNight(true, 1.9f)) << "a night that has begun holds until +2 deg";
    EXPECT_FALSE(NpcTorchPolicy::updateNight(true, 2.1f));

    // Indoors: the first sample is taken as-is; after that ON needs 0.5 s, OFF needs 2 s.
    NpcTorchState s;
    EXPECT_TRUE(NpcTorchPolicy::updateIndoors(s, true, 0.25f)) << "first sample: no latency";
    for (int i = 0; i < 7; ++i) EXPECT_TRUE(NpcTorchPolicy::updateIndoors(s, false, 0.25f)) << "sample " << i;
    EXPECT_FALSE(NpcTorchPolicy::updateIndoors(s, false, 0.25f)) << "outdoors after 2 s";
    EXPECT_FALSE(NpcTorchPolicy::updateIndoors(s, true, 0.25f));
    EXPECT_TRUE(NpcTorchPolicy::updateIndoors(s, true, 0.25f)) << "indoors after 0.5 s";
    // a single open sample (a doorway) does not flip it
    EXPECT_TRUE(NpcTorchPolicy::updateIndoors(s, false, 0.25f));
    EXPECT_TRUE(NpcTorchPolicy::updateIndoors(s, true, 0.25f));
    EXPECT_FALSE(s.pending > 0.0f) << "agreement resets the pending time";
}

// ---------------------------------------------------------------- the shelter scan on real chunks

namespace {
struct World {
    ChunkManager cm;
    World() { cm.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE); }
    Phyxel::Chunk* chunkFor(const glm::ivec3& w) {
        auto fd = [](int a) { return a >= 0 ? a / 32 : -((-a + 31) / 32); };
        const glm::ivec3 c(fd(w.x), fd(w.y), fd(w.z));
        auto it = cm.chunkMap.find(c);
        if (it != cm.chunkMap.end()) return it->second;
        auto chunk = std::make_unique<Phyxel::Chunk>(c * 32);
        chunk->initializeForLoading();
        Phyxel::Chunk* raw = chunk.get();
        cm.chunkMap[c] = raw;
        cm.chunks.push_back(std::move(chunk));
        return raw;
    }
    void cube(const glm::ivec3& w) { chunkFor(w)->addCube(w - (w / 32) * 32); }
    void ground(int x0, int x1, int z0, int z1) {
        for (int x = x0; x <= x1; ++x) for (int z = z0; z <= z1; ++z) cube(glm::ivec3(x, 15, z));
    }
    // A 7x7 room centred on (cx, cz): walls y 16..18 with a door gap on the -Z side, roof at 19.
    void room(int cx, int cz, bool microRoof = false) {
        for (int x = cx - 3; x <= cx + 3; ++x)
            for (int z = cz - 3; z <= cz + 3; ++z) {
                const bool edge = (x == cx - 3 || x == cx + 3 || z == cz - 3 || z == cz + 3);
                const bool door = (z == cz - 3 && x == cx);
                if (edge && !door) for (int y = 16; y <= 18; ++y) cube(glm::ivec3(x, y, z));
                if (!microRoof) { cube(glm::ivec3(x, 19, z)); continue; }
                // one MICRO layer of planks: 1/9 of a cube thick
                const glm::ivec3 w(x, 19, z);
                Phyxel::Chunk* ch = chunkFor(w);
                for (int sx = 0; sx < 3; ++sx) for (int sz = 0; sz < 3; ++sz)
                    for (int mx = 0; mx < 3; ++mx) for (int mz = 0; mz < 3; ++mz)
                        ch->addMicrocube(w - (w / 32) * 32, glm::ivec3(sx, 0, sz), glm::ivec3(mx, 0, mz));
            }
    }
    NpcTorchPolicy::Shelter scan(float x, float z) {
        return NpcTorchPolicy::scanShelter([this](const glm::ivec3& m) { return cm.occupiedMicro(m); },
                                           glm::vec3(x, 16.0f, z));
    }
};
}  // namespace

TEST(NpcTorchPolicyTest, ShelterScanTellsARoomFromAnAwningFromTheOpen) {
    World w;
    w.ground(0, 31, 0, 31);
    w.room(8, 8);
    const auto open = w.scan(24.5f, 24.5f);
    EXPECT_FALSE(open.roof);
    EXPECT_FALSE(open.indoors());
    const auto in = w.scan(8.5f, 8.5f);
    EXPECT_TRUE(in.roof);
    EXPECT_EQ(in.walls, 3) << "three walls and a door";
    EXPECT_TRUE(in.indoors());
    // An awning: a roof and no walls is outdoors.
    for (int x = 20; x <= 24; ++x) for (int z = 4; z <= 8; ++z) w.cube(glm::ivec3(x, 19, z));
    const auto awning = w.scan(22.5f, 6.5f);
    EXPECT_TRUE(awning.roof);
    EXPECT_FALSE(awning.indoors());
    // Resolution-complete: a roof one MICRO thick still reads as a roof.
    World m;
    m.ground(0, 31, 0, 31);
    m.room(8, 8, /*microRoof=*/true);
    EXPECT_TRUE(m.scan(8.5f, 8.5f).roof) << "a 1/9-cube plank roof must count";
    EXPECT_TRUE(m.scan(8.5f, 8.5f).indoors());
}

// Chunks must not be visible: a room straddling the chunk-0/chunk-1 border (x = 29..35) gives
// the same answer, position for position, as the same room inside one chunk (x = 5..11).
TEST(NpcDarkPlaceTest, RoofAcrossAChunkBorderGivesTheSameAnswerOnBothSides) {
    World across, inside;
    across.ground(0, 63, 0, 31);
    across.room(32, 16);
    inside.ground(0, 31, 0, 31);
    inside.room(8, 16);
    int compared = 0;
    for (float dx = -4.5f; dx <= 4.5f; dx += 0.5f) {
        for (float dz = -4.5f; dz <= 4.5f; dz += 1.5f) {
            const auto a = across.scan(32.0f + dx, 16.0f + dz);
            const auto b = inside.scan(8.0f + dx, 16.0f + dz);
            EXPECT_EQ(a.roof, b.roof) << "dx=" << dx << " dz=" << dz;
            EXPECT_EQ(a.walls, b.walls) << "dx=" << dx << " dz=" << dz;
            ++compared;
        }
    }
    EXPECT_GT(compared, 100);
    // and both sides of the border, inside the room, are indoors
    EXPECT_TRUE(across.scan(31.5f, 16.5f).indoors()) << "chunk 0 side";
    EXPECT_TRUE(across.scan(32.5f, 16.5f).indoors()) << "chunk 1 side";
}

// ---------------------------------------------------------------- integration

namespace {
struct Town {
    World world;
    Phyxel::Physics::PhysicsWorld physics;
    NPCManager npcs;
    Graphics::DayNightCycle cycle;
    KinematicVoxelManager kin;
    ObjectTemplateManager templates{nullptr, nullptr};
    ItemEffectSystem effects;
    std::map<int, glm::vec3> lights;
    int nextLight = 1;
    std::unique_ptr<NpcHeldItems> held;
    Town() {
        world.ground(0, 31, 0, 31);
        world.room(24, 24);                    // a closed room in the NE corner
        physics.initialize();
        // Collision for the rig's chunks, or every NPC falls through the floor and the shelter
        // scan runs in the void (the first version of this rig did exactly that).
        world.cm.setPhysicsWorld(&physics);
        for (auto& c : world.cm.chunks) {
            c->setPhysicsWorld(&physics);
            c->createChunkPhysicsBody();
        }
        npcs.setChunkManager(&world.cm);
        npcs.setPhysicsWorld(&physics);
        npcs.setDayNightCycle(&cycle);
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
    // 1 s of frames; `everHeld` collects every NPC that held a torch in ANY frame.
    void run(std::set<std::string>* everHeld = nullptr, int frames = 30) {
        for (int i = 0; i < frames; ++i) {
            npcs.update(1.0f / 30.0f);
            held->update(npcs);
            effects.update(1.0f / 30.0f);
            npcs.forEachNPC([&](Scene::NPCEntity& n) {
                EXPECT_GT(n.getPosition().y, 15.5f) << n.getName() << " fell through the rig floor";
            });
            if (everHeld)
                npcs.forEachNPC([&](Scene::NPCEntity& n) {
                    if (held->heldItem(n.getName()) == NpcTorchPolicy::kTorchItemId) everHeld->insert(n.getName());
                });
        }
    }
    std::set<std::string> torchBearers() {
        std::set<std::string> out;
        npcs.forEachNPC([&](Scene::NPCEntity& n) {
            if (held->heldItem(n.getName()) == NpcTorchPolicy::kTorchItemId) out.insert(n.getName());
        });
        return out;
    }
};
}  // namespace

// The contract, one small world, the clock paused: at noon only the "dark" NPC in the room
// holds a torch; at midnight the townsperson in the open lights one (and its light is at its
// hand), the hostile and the "never" control do not, and the townsperson who is INSIDE at
// nightfall never lights one - not even for one frame.
TEST(NpcTorchPolicyTest, TownsfolkLightTorchesOutdoorsAtNightAndNobodyElseDoes) {
    Town t;
    Scene::NPCEntity* ada  = t.spawn("Ada",  glm::vec3(6.5f, 16.0f, 6.5f));    // default, open
    Scene::NPCEntity* bram = t.spawn("Bram", glm::vec3(10.5f, 16.0f, 6.5f));   // hostile, open
    Scene::NPCEntity* cara = t.spawn("Cara", glm::vec3(6.5f, 16.0f, 12.5f));   // never, open
    Scene::NPCEntity* dee  = t.spawn("Dee",  glm::vec3(23.5f, 16.0f, 24.5f));  // dark, in the room
    Scene::NPCEntity* eve  = t.spawn("Eve",  glm::vec3(25.5f, 16.0f, 24.5f));  // default, in the room
    ASSERT_TRUE(ada && bram && cara && dee && eve) << "run from the repo root";
    bram->setHostile(true);
    cara->torchState().policy = TorchPolicy::Never;
    dee->torchState().policy = TorchPolicy::Dark;

    t.cycle.setTimeOfDay(12.0f);
    t.run();
    EXPECT_FALSE(t.npcs.isTorchNight());
    EXPECT_EQ(t.torchBearers(), (std::set<std::string>{"Dee"})) << "noon: only the dark-policy NPC indoors";
    EXPECT_EQ(t.lights.size(), 1u);

    t.cycle.setTimeOfDay(0.0f);
    std::set<std::string> ever;
    t.run(&ever);
    EXPECT_TRUE(t.npcs.isTorchNight());
    {
        const glm::vec3 e = eve->getPosition();
        const auto sh = t.world.scan(e.x, e.z);
        EXPECT_EQ(t.torchBearers(), (std::set<std::string>{"Ada", "Dee"}))
            << "midnight. Eve at (" << e.x << "," << e.y << "," << e.z << ") roof=" << sh.roof
            << " walls=" << sh.walls << " latched indoors=" << eve->torchState().indoors;
    }
    EXPECT_EQ(ever.count("Eve"), 0u) << "Eve was inside at nightfall and must never light up";
    EXPECT_EQ(ever.count("Bram"), 0u);
    EXPECT_EQ(ever.count("Cara"), 0u);
    ASSERT_EQ(t.lights.size(), 2u) << "one flame light per torch";
    // Ada's light rides her hand: within 0.5 of her torch mesh, above her feet.
    const glm::vec3 grip(t.kin.getTransform(t.held->heldMeshId("Ada"))[3]);
    float best = 1e9f;
    for (const auto& [id, p] : t.lights) best = std::min(best, glm::length(p - grip));
    EXPECT_LT(best, 0.5f) << "no light near Ada's hand";
    EXPECT_EQ(NpcTorchPolicy::name(t.npcs.resolvedTorchPolicy(*ada)), std::string("night"));
    EXPECT_EQ(NpcTorchPolicy::name(t.npcs.resolvedTorchPolicy(*bram)), std::string("never"));

    // Morning: the townsperson's torch goes out, the dark-policy one keeps hers.
    t.cycle.setTimeOfDay(12.0f);
    t.run();
    EXPECT_EQ(t.torchBearers(), (std::set<std::string>{"Dee"}));
    EXPECT_EQ(t.lights.size(), 1u) << "put-out torches must take their light";
}

// Walking out of the room at night lights the torch within the latch time, walking back in puts
// it out - and a carried item that is NOT the torch is never replaced by one.
TEST(NpcTorchPolicyTest, LeavingTheRoomAtNightLightsTheTorchAndOtherItemsAreKept) {
    Town t;
    Scene::NPCEntity* eve = t.spawn("Eve", glm::vec3(25.5f, 16.0f, 24.5f));
    Scene::NPCEntity* fin = t.spawn("Fin", glm::vec3(6.5f, 16.0f, 6.5f));
    ASSERT_TRUE(eve && fin);
    fin->setCarriedItem("iron_sword");
    t.cycle.setTimeOfDay(0.0f);
    t.run();
    EXPECT_EQ(t.held->heldItem("Eve"), "");
    EXPECT_EQ(fin->getCarriedItem(), "iron_sword") << "the torch policy must not take a carried item away";

    eve->setPosition(glm::vec3(10.5f, 16.0f, 10.5f));   // out in the street
    t.run(nullptr, 3 * 30);
    EXPECT_EQ(t.held->heldItem("Eve"), "torch") << "outdoors at night for 3 s";
    eve->setPosition(glm::vec3(25.5f, 16.0f, 24.5f));   // back inside
    t.run(nullptr, 2 * 30);
    EXPECT_EQ(t.held->heldItem("Eve"), "") << "inside for 2 s: the torch is out";
}
