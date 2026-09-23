#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>

#include "core/Chunk.h"
#include "core/MaterialRegistry.h"

// Chunk::recomputeRenderFlags() — the cached per-chunk render flags that gate whole render passes.
// docs/GlassTransparency.md §4.
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

TEST_F(ChunkRenderFlagsTest, SubVoxelGlassMarksTheChunkTransparent) {
    ASSERT_TRUE(loaded_);
    auto chunk = makeChunk();
    ASSERT_TRUE(chunk->addSubcube(glm::ivec3(4, 4, 4), glm::ivec3(0, 0, 0), "Glass"));
    ASSERT_EQ(chunk->getStaticSubcubeCount(), 1u) << "precondition: the glass subcube was placed";

    chunk->rebuildFaces();

    EXPECT_TRUE(chunk->hasTransparentVoxel())
        << "a chunk whose ONLY glass is a subcube still contains glass. The flag gates the whole "
           "OIT pass (RenderCoordinator.cpp:1904), so reporting false here renders that glass "
           "opaque — and every generated building's window is sub-voxel.";
}

TEST_F(ChunkRenderFlagsTest, MicrocubeGlassMarksTheChunkTransparent) {
    ASSERT_TRUE(loaded_);
    auto chunk = makeChunk();
    ASSERT_TRUE(chunk->addSubcube(glm::ivec3(4, 4, 4), glm::ivec3(0, 0, 0), "Glass"));
    ASSERT_TRUE(chunk->addMicrocube(glm::ivec3(6, 6, 6), glm::ivec3(0, 0, 0), glm::ivec3(0, 0, 0),
                                    "Glass"));

    chunk->rebuildFaces();

    EXPECT_TRUE(chunk->hasTransparentVoxel())
        << "microcube glass is the finest tier and the one interior trim actually uses";
}

// Same scan, same omission, different flag: a sub-voxel mirror is invisible to the reflection pass
// for exactly the reason glass is invisible to the transparent pass. Pinned here so the fix closes
// the whole defect rather than the half that was reported.
TEST_F(ChunkRenderFlagsTest, SubVoxelMirrorMarksTheChunkMirrored) {
    ASSERT_TRUE(loaded_);
    const auto* mirror = MaterialRegistry::instance().getMaterial("Mirror");
    ASSERT_NE(mirror, nullptr) << "Mirror is missing from materials.json";
    ASSERT_TRUE(mirror->isMirror) << "Mirror is no longer flagged isMirror";

    auto chunk = makeChunk();
    ASSERT_TRUE(chunk->addSubcube(glm::ivec3(4, 4, 4), glm::ivec3(0, 0, 0), "Mirror"));
    chunk->rebuildFaces();

    EXPECT_TRUE(chunk->hasMirrorVoxel())
        << "a sub-voxel mirror is still a mirror; the reflection pass is gated on this flag";
}

// EVERY face-install path must leave the flag correct (docs/GlassTransparency.md §13.13).
//
// Under the §13.2 routing (transparent faces drawn by the OIT pass ONLY), a wrong
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
        {"subcube",   [](Chunk& c) { return c.addSubcube(glm::ivec3(8, 8, 8), glm::ivec3(1, 1, 1), "Glass"); }},
        {"microcube", [](Chunk& c) { return c.addMicrocube(glm::ivec3(12, 12, 12), glm::ivec3(1, 1, 1), glm::ivec3(1, 1, 1), "Glass"); }},
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
