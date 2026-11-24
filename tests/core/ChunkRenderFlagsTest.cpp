#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>

#include "core/Chunk.h"
#include "core/MaterialRegistry.h"

// Chunk::recomputeRenderFlags() — the cached per-chunk render flags that gate whole render passes.
// docs/GlassTransparency.md §1.
//
// WHY THIS FILE EXISTS. `m_hasTransparent` decides whether the OIT transparent pass runs at all
// (RenderCoordinator.cpp:1898-1904, a frame-global early-out), and `m_hasMirror` likewise gates the
// reflection pass. Both are computed by one scan in recomputeRenderFlags(), and before this file
// NOTHING in the test suite touched either — a flag that can silently turn off a render pass for a
// whole frame had no pin at all.
//
// THE DESIGN KEY AT STAKE (docs/FeatureDesignKeys.md): "Appearance must be a pure function of world
// position and persistent world state. Per-chunk quantities may only bound COST — never how
// something looks." A cached per-chunk flag gating a pass is legitimate *only while it is
// conservative*: it may say "there might be glass here" when there is none (wasted work), but never
// "there is no glass here" when there is (wrong picture). These tests pin exactly that direction.

namespace Phyxel {
namespace Testing {

using Phyxel::Core::MaterialRegistry;

namespace {

std::string findMaterialsJson() {
    for (const auto& path : {
        "resources/materials.json",
        "../resources/materials.json",
        "../../resources/materials.json",
        "../../../resources/materials.json"
    }) {
        if (std::filesystem::exists(path)) return path;
    }
    return "resources/materials.json";
}

class ChunkRenderFlagsTest : public ::testing::Test {
protected:
    void SetUp() override {
        loaded_ = MaterialRegistry::instance().loadFromJson(findMaterialsJson());
    }

    // recomputeRenderFlags() is private and is called by rebuildFaces(), which is how production
    // reaches it too — so the test drives the real path rather than a test-only back door.
    std::unique_ptr<Chunk> makeChunk() {
        auto chunk = std::make_unique<Chunk>(glm::ivec3(0));
        chunk->initializeForLoading();
        return chunk;
    }

    bool loaded_ = false;
};

} // namespace

// ---------------------------------------------------------------------------------------------
// CONTROLS. These must PASS today. Without them a failure below is ambiguous: it could equally
// mean "the flag is broken" or "this test never built a transparent voxel in the first place" —
// e.g. Glass silently renamed, alpha changed, or rebuildFaces() not actually recomputing.
// ---------------------------------------------------------------------------------------------

TEST_F(ChunkRenderFlagsTest, ControlPristineChunkIsNeitherTransparentNorMirrored) {
    ASSERT_TRUE(loaded_);
    auto chunk = makeChunk();
    chunk->rebuildFaces();
    EXPECT_FALSE(chunk->hasTransparentVoxel()) << "an empty chunk cannot contain glass";
    EXPECT_FALSE(chunk->hasMirrorVoxel())      << "an empty chunk cannot contain a mirror";
}

TEST_F(ChunkRenderFlagsTest, ControlGlassAsAFullCubeMarksTheChunkTransparent) {
    ASSERT_TRUE(loaded_);
    const auto* glass = MaterialRegistry::instance().getMaterial("Glass");
    ASSERT_NE(glass, nullptr) << "Glass is missing from materials.json";
    ASSERT_LT(glass->alpha, 0.99f)
        << "Glass no longer has alpha < 0.99, so it is not transparent by the engine's own "
           "criterion and every test below is measuring nothing";

    auto chunk = makeChunk();
    ASSERT_TRUE(chunk->addCube(glm::ivec3(4, 4, 4), "Glass", true));
    chunk->rebuildFaces();

    EXPECT_TRUE(chunk->hasTransparentVoxel())
        << "full-cube glass must mark the chunk transparent — if THIS fails the harness is wrong, "
           "not the engine";
}

// ---------------------------------------------------------------------------------------------
// THE DEFECT. recomputeRenderFlags() walks the CUBE store only (Chunk.cpp:416-438) — it iterates
// ChunkVoxelStore::kVoxels and consults `cubes[i]` or the store, and never looks at
// staticSubcubes / staticMicrocubes. Yet both sub-voxel instance paths DO set the transparent bit
// and quantized alpha (ChunkRenderManager.cpp:1084, :1244), so the geometry is submitted expecting
// the OIT pass to run for it.
//
// Consequence, which is what makes this worth a test rather than a comment: generated walls are
// subcube/microcube (StructureRealizer stamps via fillMicroBox), so EVERY window in EVERY generated
// building takes this path. A view whose only glass is sub-voxel skips the transparent pass
// entirely and renders that glass opaque — and whether it does depends on whether some unrelated
// full-cube glass happens to be in view, which is appearance coupled to chunk contents.
// ---------------------------------------------------------------------------------------------

// EVERY face-install path must leave the flag correct (docs/GlassTransparency.md §1).
//
// Under the GlassTransparency.md §1 routing (transparent faces drawn by the OIT pass ONLY), a wrong
// hasTransparentVoxel() no longer renders glass opaque -- it renders it INVISIBLE: the opaque pass
// discards the face and the OIT pass is skipped for the frame. So the invariant is now
// "no face is ever both discarded by the opaque pass and skipped by OIT".
//
// rebuildFaces() refreshes the flag; setLodFaces() installed faces WITHOUT doing so, so a chunk that
// only ever received LOD faces kept the default `false`.
TEST_F(ChunkRenderFlagsTest, SetLodFacesLeavesTheTransparentFlagCorrect) {
    ASSERT_TRUE(loaded_);
    auto chunk = makeChunk();
    ASSERT_TRUE(chunk->addCube(glm::ivec3(4, 4, 4), "Glass", true));
    // Deliberately NO rebuildFaces(): this is the chunk that only ever gets LOD geometry.
    chunk->setLodFaces({}, 1);
    EXPECT_TRUE(chunk->hasTransparentVoxel())
        << "a chunk containing glass that received its faces through setLodFaces() reports no "
           "transparent voxel -- under OIT-only routing that glass would be invisible";
}

TEST_F(ChunkRenderFlagsTest, SetLodFacesLeavesTheMirrorFlagCorrect) {
    ASSERT_TRUE(loaded_);
    auto chunk = makeChunk();
    ASSERT_TRUE(chunk->addCube(glm::ivec3(4, 4, 4), "Mirror", true));
    chunk->setLodFaces({}, 1);
    EXPECT_TRUE(chunk->hasMirrorVoxel())
        << "same install path, same omission, for the reflection pass";
}

// The invariant stated directly, and the one that matters for the design key: the flag must never
// be LESS true than the chunk's contents. Adding transparent geometry can only ever turn it on.
TEST_F(ChunkRenderFlagsTest, TheFlagIsConservativeAcrossEveryVoxelTier) {
    ASSERT_TRUE(loaded_);
    struct Tier { const char* name; bool (*place)(Chunk&); };
    const Tier tiers[] = {
        {"cube",      [](Chunk& c) { return c.addCube(glm::ivec3(2, 2, 2), "Glass", true); }},
    };
    for (const auto& tier : tiers) {
        auto chunk = makeChunk();
        ASSERT_TRUE(tier.place(*chunk)) << "could not place glass as a " << tier.name;
        chunk->rebuildFaces();
        EXPECT_TRUE(chunk->hasTransparentVoxel())
            << "glass placed as a " << tier.name << " left the chunk marked NOT transparent. "
               "The flag may over-report (cost) but never under-report (appearance).";
    }
}

} // namespace Testing
} // namespace Phyxel
