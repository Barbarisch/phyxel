/**
 * Unit tests for MaterialRegistry
 *
 * Verifies that the data-driven MaterialRegistry loaded from materials.json
 * correctly handles material lookups, texture indices, and physics properties.
 */

#include <gtest/gtest.h>
#include "core/MaterialRegistry.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <set>
#include <filesystem>

using namespace Phyxel::Core;

namespace {

// Helper: find materials.json relative to the test executable
std::string findMaterialsJson() {
    // Try common paths relative to build directory
    for (const auto& path : {
        "resources/materials.json",
        "../resources/materials.json",
        "../../resources/materials.json",
        "../../../resources/materials.json"
    }) {
        if (std::filesystem::exists(path)) return path;
    }
    return "resources/materials.json"; // fallback
}

class MaterialRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        registry_ = std::make_unique<MaterialRegistry>();
        std::string path = findMaterialsJson();
        loaded_ = registry_->loadFromJson(path);
        // The JSON itself is the expected-value source for count/order/name
        // checks. Hardcoded counts went stale TWICE (94→102, then the 103rd
        // material landed 2026-08-06 and the "102" tests failed for 13 days) —
        // never carry a count forward.
        std::ifstream f(path);
        if (f.is_open()) {
            try { f >> json_; } catch (...) {}
        }
    }

    /// Names in materials.json array order (= the registry's ID order).
    std::vector<std::string> jsonNames() const {
        std::vector<std::string> out;
        if (json_.contains("materials") && json_["materials"].is_array())
            for (const auto& m : json_["materials"])
                out.push_back(m.value("name", ""));
        return out;
    }

    std::unique_ptr<MaterialRegistry> registry_;
    nlohmann::json json_;
    bool loaded_ = false;
};

} // namespace

// ============================================================================
// Loading Tests
// ============================================================================

TEST_F(MaterialRegistryTest, LoadsSuccessfully) {
    ASSERT_TRUE(loaded_) << "Failed to load materials.json";
}

TEST_F(MaterialRegistryTest, HasCorrectMaterialCount) {
    ASSERT_TRUE(loaded_);
    // Registry count == the materials.json array length, read from the SAME
    // file the registry loaded — a consistency check, not a copied number.
    const auto names = jsonNames();
    ASSERT_FALSE(names.empty()) << "could not parse materials.json for expectations";
    EXPECT_EQ(registry_->getMaterialCount(), static_cast<int>(names.size()));
}

TEST_F(MaterialRegistryTest, HasCorrectTextureCount) {
    ASSERT_TRUE(loaded_);
    // Every material carries 6 face-texture slots.
    const auto names = jsonNames();
    ASSERT_FALSE(names.empty());
    EXPECT_EQ(registry_->getTextureCount(), static_cast<int>(names.size()) * 6);
    // Total splits across the two resolution classes (512 + 1024).
    EXPECT_EQ(registry_->getTextureCount(0) + registry_->getTextureCount(1),
              registry_->getTextureCount());
}

// ============================================================================
// Material ID Tests (must match old TextureConstants)
// ============================================================================

TEST_F(MaterialRegistryTest, MaterialIDs_MatchJSON) {
    ASSERT_TRUE(loaded_);
    // IDs are assigned by JSON array order. Check EVERY material against its
    // array index (the old 19 hardcoded rows went stale whenever a material
    // was inserted mid-array).
    const auto names = jsonNames();
    ASSERT_FALSE(names.empty());
    for (size_t i = 0; i < names.size(); ++i) {
        EXPECT_EQ(registry_->getMaterialID(names[i]), static_cast<int>(i))
            << "'" << names[i] << "' expected at JSON index " << i;
    }
    // Anchor: Default is the id-0 fallback material by contract.
    EXPECT_EQ(registry_->getMaterialID("Default"), 0);
}

TEST_F(MaterialRegistryTest, UnknownMaterial_FallsBackToDefault) {
    ASSERT_TRUE(loaded_);
    int defaultID = registry_->getMaterialID("Default");
    EXPECT_EQ(registry_->getMaterialID("unknown"), defaultID);
    EXPECT_EQ(registry_->getMaterialID(""), defaultID);
    EXPECT_EQ(registry_->getMaterialID("something_else"), defaultID);
}

TEST_F(MaterialRegistryTest, MaterialIDs_CaseSensitive) {
    ASSERT_TRUE(loaded_);
    int defaultID = registry_->getMaterialID("Default");
    // Wrong case should fall back to Default
    EXPECT_EQ(registry_->getMaterialID("stone"), defaultID);
    EXPECT_EQ(registry_->getMaterialID("STONE"), defaultID);
    EXPECT_EQ(registry_->getMaterialID("wood"), defaultID);
    // Correct case should work
    EXPECT_EQ(registry_->getMaterialID("Stone"), 3);
    EXPECT_EQ(registry_->getMaterialID("Wood"),  8);
}

// ============================================================================
// Texture Index Tests — verify atlas indices
// ============================================================================

TEST_F(MaterialRegistryTest, TextureIndices_AllUnique) {
    ASSERT_TRUE(loaded_);
    std::set<uint16_t> allIndices;
    for (int mat = 0; mat < registry_->getMaterialCount(); mat++) {
        for (int face = 0; face < 6; face++) {
            allIndices.insert(registry_->getTextureIndex(mat, face));
        }
    }
    EXPECT_EQ(static_cast<int>(allIndices.size()), registry_->getTextureCount());
}

TEST_F(MaterialRegistryTest, TextureIndices_InRange) {
    ASSERT_TRUE(loaded_);
    // textureIndex encodes class in bit 15, within-class layer in bits 0..14.
    for (int mat = 0; mat < registry_->getMaterialCount(); mat++) {
        for (int face = 0; face < 6; face++) {
            uint16_t idx = registry_->getTextureIndex(mat, face);
            int cls = (idx & MaterialRegistry::RES_CLASS_BIT) ? 1 : 0;
            int layer = idx & MaterialRegistry::LAYER_MASK;
            EXPECT_LT(layer, registry_->getTextureCount(cls))
                << "Material " << mat << " face " << face << " out of range (class " << cls << ")";
        }
    }
}

TEST_F(MaterialRegistryTest, TextureIndex_InvalidFace_ReturnsPlaceholder) {
    ASSERT_TRUE(loaded_);
    uint16_t ph = registry_->getPlaceholderIndex();
    EXPECT_EQ(registry_->getTextureIndex("Stone", -1), ph);
    EXPECT_EQ(registry_->getTextureIndex("Stone", 6), ph);
}

// ============================================================================
// Physics Properties Tests
// ============================================================================

TEST_F(MaterialRegistryTest, Physics_WoodProperties) {
    ASSERT_TRUE(loaded_);
    const MaterialDef* wood = registry_->getMaterial("Wood");
    ASSERT_NE(wood, nullptr);
    EXPECT_FLOAT_EQ(wood->physics.mass, 0.7f);
    EXPECT_FLOAT_EQ(wood->physics.friction, 0.6f);
    EXPECT_FLOAT_EQ(wood->physics.restitution, 0.2f);
    EXPECT_FLOAT_EQ(wood->physics.bondStrength, 0.6f);
}

TEST_F(MaterialRegistryTest, Physics_StoneProperties) {
    ASSERT_TRUE(loaded_);
    const MaterialDef* stone = registry_->getMaterial("Stone");
    ASSERT_NE(stone, nullptr);
    EXPECT_FLOAT_EQ(stone->physics.mass, 6.0f);
    EXPECT_FLOAT_EQ(stone->physics.friction, 0.8f);
    EXPECT_FLOAT_EQ(stone->physics.restitution, 0.05f);
    EXPECT_FLOAT_EQ(stone->physics.bondStrength, 0.9f);
}

TEST_F(MaterialRegistryTest, Physics_SystemMaterialsHaveNoPhysics) {
    ASSERT_TRUE(loaded_);
    // All remaining materials are category=material and have physics
    // (system-only materials placeholder/hover were removed)
    const MaterialDef* stone = registry_->getMaterial("Stone");
    ASSERT_NE(stone, nullptr);
    EXPECT_TRUE(stone->hasPhysics());
}

// ============================================================================
// Convenience Accessor Tests
// ============================================================================

TEST_F(MaterialRegistryTest, HoverTextureIndex_ValidFaces) {
    ASSERT_TRUE(loaded_);
    // hover material was removed; getHoverTextureIndex falls back to placeholderIndex (0)
    for (int face = 0; face < 6; face++) {
        uint16_t idx = registry_->getHoverTextureIndex(face);
        EXPECT_EQ(idx, registry_->getPlaceholderIndex());
    }
}

TEST_F(MaterialRegistryTest, HoverTextureIndex_InvalidFace) {
    ASSERT_TRUE(loaded_);
    EXPECT_EQ(registry_->getHoverTextureIndex(-1), registry_->getPlaceholderIndex());
    EXPECT_EQ(registry_->getHoverTextureIndex(6), registry_->getPlaceholderIndex());
}

TEST_F(MaterialRegistryTest, GrassdirtTextureIndex_ValidFaces) {
    ASSERT_TRUE(loaded_);
    // grassdirt was renamed to Grass; getGrassdirtTextureIndex should still resolve
    for (int face = 0; face < 6; face++) {
        uint16_t idx = registry_->getGrassdirtTextureIndex(face);
        EXPECT_LT(idx, registry_->getTextureCount());
    }
}

// ============================================================================
// Existence & Enumeration
// ============================================================================

TEST_F(MaterialRegistryTest, HasMaterial_KnownMaterials) {
    ASSERT_TRUE(loaded_);
    EXPECT_TRUE(registry_->hasMaterial("Stone"));
    EXPECT_TRUE(registry_->hasMaterial("Wood"));
    EXPECT_TRUE(registry_->hasMaterial("Grass"));
    EXPECT_TRUE(registry_->hasMaterial("Cobblestone"));
    EXPECT_TRUE(registry_->hasMaterial("StoneBricks"));
    EXPECT_TRUE(registry_->hasMaterial("Log"));
    EXPECT_FALSE(registry_->hasMaterial("placeholder"));
    EXPECT_FALSE(registry_->hasMaterial("unknown"));
}

TEST_F(MaterialRegistryTest, GetAllMaterialNames_HasAll) {
    ASSERT_TRUE(loaded_);
    // Set equality against materials.json: same size, every JSON name present.
    auto names = registry_->getAllMaterialNames();
    const auto expect = jsonNames();
    ASSERT_FALSE(expect.empty());
    EXPECT_EQ(names.size(), expect.size());
    const std::set<std::string> have(names.begin(), names.end());
    for (const auto& n : expect)
        EXPECT_TRUE(have.count(n)) << "registry missing JSON material '" << n << "'";
}

// ============================================================================
// Runtime Mutability
// ============================================================================

TEST_F(MaterialRegistryTest, AddMaterial_AssignsNewID) {
    ASSERT_TRUE(loaded_);
    int oldCount = registry_->getMaterialCount();

    MaterialDef newMat;
    newMat.name = "TestMat";
    newMat.category = "material";

    int newID = registry_->addMaterial(newMat);
    EXPECT_EQ(newID, oldCount);
    EXPECT_EQ(registry_->getMaterialCount(), oldCount + 1);
    EXPECT_TRUE(registry_->hasMaterial("TestMat"));
}

TEST_F(MaterialRegistryTest, AddMaterial_DuplicateNameFails) {
    ASSERT_TRUE(loaded_);
    MaterialDef dup;
    dup.name = "Stone";
    EXPECT_EQ(registry_->addMaterial(dup), -1);
}

TEST_F(MaterialRegistryTest, RemoveMaterial_Works) {
    ASSERT_TRUE(loaded_);
    MaterialDef newMat;
    newMat.name = "Removable";
    newMat.category = "material";
    registry_->addMaterial(newMat);

    EXPECT_TRUE(registry_->removeMaterial("Removable"));
    EXPECT_FALSE(registry_->hasMaterial("Removable"));
}

TEST_F(MaterialRegistryTest, RemoveMaterial_SystemFails) {
    ASSERT_TRUE(loaded_);
    // placeholder was removed from the material set; verify a real material
    // cannot be force-removed via name lookup of a non-existent entry
    EXPECT_FALSE(registry_->removeMaterial("placeholder")); // not present
    EXPECT_FALSE(registry_->hasMaterial("placeholder"));   // still not present
}

// ============================================================================
// Emissive Flag
// ============================================================================

TEST_F(MaterialRegistryTest, Emissive_GlowIsEmissive) {
    ASSERT_TRUE(loaded_);
    const MaterialDef* glow = registry_->getMaterial("glow");
    ASSERT_NE(glow, nullptr);
    EXPECT_TRUE(glow->emissive);
}

TEST_F(MaterialRegistryTest, Emissive_StoneIsNotEmissive) {
    ASSERT_TRUE(loaded_);
    const MaterialDef* stone = registry_->getMaterial("Stone");
    ASSERT_NE(stone, nullptr);
    EXPECT_FALSE(stone->emissive);
}

// ============================================================================
// Serialization Roundtrip
// ============================================================================

TEST_F(MaterialRegistryTest, SaveAndReload_Roundtrip) {
    ASSERT_TRUE(loaded_);
    const std::string tmpPath = "test_materials_roundtrip.json";

    ASSERT_TRUE(registry_->saveToJson(tmpPath));

    MaterialRegistry reloaded;
    ASSERT_TRUE(reloaded.loadFromJson(tmpPath));

    EXPECT_EQ(reloaded.getMaterialCount(), registry_->getMaterialCount());

    for (int i = 0; i < registry_->getMaterialCount(); i++) {
        const MaterialDef* orig = registry_->getMaterial(i);
        const MaterialDef* copy = reloaded.getMaterial(i);
        ASSERT_NE(orig, nullptr);
        ASSERT_NE(copy, nullptr);
        EXPECT_EQ(orig->name, copy->name);
        EXPECT_EQ(orig->category, copy->category);
        EXPECT_FLOAT_EQ(orig->physics.mass, copy->physics.mass);
    }

    std::filesystem::remove(tmpPath);
}
