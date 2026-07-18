// ChunkRenderBuffer arena mode — Phase 4.3 increment A1 (docs/RegionArenaPlan.md).
// Red-first: written before the arena branches exist in ChunkRenderBuffer; headless
// (test-mode ChunkArenaSystem, VK_NULL_HANDLE device) so the paths run without Vulkan.

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "graphics/ChunkArenaSystem.h"
#include "graphics/ChunkRenderBuffer.h"

using Phyxel::Graphics::ChunkArenaAllocator;
using Phyxel::Graphics::ChunkArenaSystem;
using Phyxel::Graphics::ChunkRenderBuffer;

namespace {

// RAII: arena test mode + toggle on, restored after each test.
class ArenaFixture : public ::testing::Test {
protected:
    void SetUp() override {
        ChunkArenaSystem::instance().initializeForTests(1 << 20);
        ChunkRenderBuffer::s_regionArenas = true;
    }
    void TearDown() override {
        ChunkRenderBuffer::s_regionArenas = false;
        ChunkArenaSystem::instance().shutdown();
    }
    ChunkArenaAllocator* alloc() { return ChunkArenaSystem::instance().allocator(); }
};

std::vector<uint8_t> patternBytes(size_t n, uint8_t seed) {
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = uint8_t(seed + i * 31);
    return v;
}

} // namespace

TEST_F(ArenaFixture, CreateAllocatesSpanAndCopiesData) {
    ChunkRenderBuffer buf(VK_NULL_HANDLE, VK_NULL_HANDLE);
    buf.setRegionKey(42);
    auto data = patternBytes(1000, 7);
    buf.createBufferRaw(data.data(), 100, 10, 0);  // 100 elems x 10 B

    EXPECT_TRUE(buf.isArenaMode());
    ASSERT_TRUE(buf.getSpan().valid());
    ASSERT_NE(buf.getMappedMemory(), nullptr);
    EXPECT_EQ(std::memcmp(buf.getMappedMemory(), data.data(), data.size()), 0);
    EXPECT_GE(buf.getCapacity(), 100u);
    EXPECT_EQ(alloc()->liveSpans(), 1u);
    // No 25000-instance floor in arena mode: span stays measured in KB, not 586 KB.
    EXPECT_LT(buf.getSpan().capacity, 16u * 1024u);
    EXPECT_EQ(buf.getBindOffsetBytes(), buf.getSpan().offset);
}

TEST_F(ArenaFixture, GrowthMovesToLargerSpanAndRetiresOld) {
    ChunkRenderBuffer buf(VK_NULL_HANDLE, VK_NULL_HANDLE);
    buf.setRegionKey(42);
    auto data = patternBytes(240, 3);
    buf.createBufferRaw(data.data(), 10, 24, 0);
    const size_t oldCap = buf.getCapacity();

    buf.reallocateBuffer(oldCap * 4);
    EXPECT_GE(buf.getCapacity(), oldCap * 4);
    EXPECT_TRUE(buf.getSpan().valid());
    ASSERT_NE(buf.getMappedMemory(), nullptr);
    // Old span is retired, new one live: exactly one live span.
    EXPECT_EQ(alloc()->liveSpans(), 1u);
    // The mapping is writable at the new capacity (callers rewrite fully after grow).
    std::vector<uint8_t> big(buf.getCapacity() * 24, 0xAB);
    std::memcpy(buf.getMappedMemory(), big.data(), big.size());
}

TEST_F(ArenaFixture, CleanupRetiresSpan) {
    ChunkRenderBuffer buf(VK_NULL_HANDLE, VK_NULL_HANDLE);
    buf.setRegionKey(1);
    auto data = patternBytes(100, 1);
    buf.createBufferRaw(data.data(), 10, 10, 0);
    EXPECT_EQ(alloc()->liveSpans(), 1u);
    buf.cleanup();
    EXPECT_EQ(alloc()->liveSpans(), 0u);
    EXPECT_FALSE(buf.getSpan().valid());
    EXPECT_EQ(buf.getMappedMemory(), nullptr);
    // Reusable: create again after cleanup.
    buf.createBufferRaw(data.data(), 10, 10, 0);
    EXPECT_TRUE(buf.getSpan().valid());
}

TEST_F(ArenaFixture, SameRegionSharesBlockDifferentRegionsDoNot) {
    ChunkRenderBuffer a(VK_NULL_HANDLE, VK_NULL_HANDLE);
    ChunkRenderBuffer b(VK_NULL_HANDLE, VK_NULL_HANDLE);
    ChunkRenderBuffer c(VK_NULL_HANDLE, VK_NULL_HANDLE);
    a.setRegionKey(7);
    b.setRegionKey(7);
    c.setRegionKey(9);
    auto data = patternBytes(256, 5);
    a.createBufferRaw(data.data(), 16, 16, 0);
    b.createBufferRaw(data.data(), 16, 16, 0);
    c.createBufferRaw(data.data(), 16, 16, 0);
    ASSERT_TRUE(a.getSpan().valid() && b.getSpan().valid() && c.getSpan().valid());
    EXPECT_EQ(a.getSpan().blockId, b.getSpan().blockId);
    EXPECT_NE(a.getSpan().offset, b.getSpan().offset);
    EXPECT_NE(a.getSpan().blockId, c.getSpan().blockId);
}

TEST_F(ArenaFixture, MoveTransfersSpanOwnership) {
    ChunkRenderBuffer a(VK_NULL_HANDLE, VK_NULL_HANDLE);
    a.setRegionKey(3);
    auto data = patternBytes(100, 9);
    a.createBufferRaw(data.data(), 10, 10, 0);
    const auto span = a.getSpan();

    ChunkRenderBuffer b(std::move(a));
    EXPECT_TRUE(b.isArenaMode());
    EXPECT_EQ(b.getSpan().blockId, span.blockId);
    EXPECT_EQ(b.getSpan().offset, span.offset);
    EXPECT_FALSE(a.getSpan().valid());  // NOLINT(bugprone-use-after-move) — asserting moved-from state
    EXPECT_EQ(alloc()->liveSpans(), 1u);
    b.cleanup();
    EXPECT_EQ(alloc()->liveSpans(), 0u);
}

TEST(ChunkRenderBufferArenaRegionKey, RegionKeyGroups8x4x8AndSeparatesNeighbors) {
    using Phyxel::Graphics::ChunkArenaSystem;
    // Chunks inside the same 8x4x8-chunk region share a key...
    const uint64_t k1 = ChunkArenaSystem::regionKeyForChunkOrigin({0, 0, 0});
    const uint64_t k2 = ChunkArenaSystem::regionKeyForChunkOrigin({7 * 32, 3 * 32, 7 * 32});
    EXPECT_EQ(k1, k2);
    // ...the next region over differs, on each axis.
    EXPECT_NE(k1, ChunkArenaSystem::regionKeyForChunkOrigin({8 * 32, 0, 0}));
    EXPECT_NE(k1, ChunkArenaSystem::regionKeyForChunkOrigin({0, 4 * 32, 0}));
    EXPECT_NE(k1, ChunkArenaSystem::regionKeyForChunkOrigin({0, 0, 8 * 32}));
    // Negative coordinates: floor semantics, distinct from the positive side.
    const uint64_t kn = ChunkArenaSystem::regionKeyForChunkOrigin({-32, 0, 0});
    EXPECT_NE(kn, k1);
    EXPECT_EQ(kn, ChunkArenaSystem::regionKeyForChunkOrigin({-8 * 32, 0, 0}));
}
