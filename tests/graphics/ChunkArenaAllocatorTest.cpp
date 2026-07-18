// ChunkArenaAllocator unit suite — Phase 4.3 increment A0 (docs/RegionArenaPlan.md).
// Red-first: written against the interface before the implementation; the stub
// allocator fails every one of these. Pure CPU (mock backend, no Vulkan).

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <vector>

#include "graphics/ChunkArenaAllocator.h"

using Phyxel::Graphics::ArenaBlockBackend;
using Phyxel::Graphics::ArenaSpan;
using Phyxel::Graphics::ChunkArenaAllocator;

namespace {

// Mock backend: hands out sequential handles, records live blocks + sizes.
struct MockBackend : ArenaBlockBackend {
    int next = 0;
    std::set<int> live;
    std::vector<size_t> createdSizes;
    bool failNext = false;

    int createBlock(size_t bytes) override {
        if (failNext) { failNext = false; return -1; }
        createdSizes.push_back(bytes);
        live.insert(next);
        return next++;
    }
    void destroyBlock(int handle) override {
        ASSERT_TRUE(live.count(handle)) << "destroying unknown/dead block " << handle;
        live.erase(handle);
    }
};

// Overlap invariant: no two live spans in the same block may intersect.
class SpanTracker {
public:
    void add(const ArenaSpan& s) {
        for (const ArenaSpan& o : m_live) {
            if (o.blockId != s.blockId) continue;
            const bool disjoint =
                s.offset + s.capacity <= o.offset || o.offset + o.capacity <= s.offset;
            ASSERT_TRUE(disjoint) << "span overlap in block " << s.blockId << ": ["
                                  << s.offset << "," << s.offset + s.capacity << ") vs ["
                                  << o.offset << "," << o.offset + o.capacity << ")";
        }
        m_live.push_back(s);
    }
    ArenaSpan take(size_t i) {
        ArenaSpan s = m_live.at(i);
        m_live.erase(m_live.begin() + i);
        return s;
    }
    size_t count() const { return m_live.size(); }

private:
    std::vector<ArenaSpan> m_live;
};

constexpr size_t kTestBlock = 4096;  // small blocks so chaining/eviction is cheap to hit

} // namespace

TEST(ChunkArenaAllocatorTest, AllocatesAlignedSpansThatFit) {
    MockBackend be;
    ChunkArenaAllocator a(&be, kTestBlock, 4);
    ArenaSpan s1 = a.allocate(1, 100);
    ArenaSpan s2 = a.allocate(1, 300);
    ASSERT_TRUE(s1.valid());
    ASSERT_TRUE(s2.valid());
    EXPECT_EQ(s1.offset % ChunkArenaAllocator::kAlignment, 0u);
    EXPECT_EQ(s2.offset % ChunkArenaAllocator::kAlignment, 0u);
    EXPECT_GE(s1.capacity, 100u);
    EXPECT_GE(s2.capacity, 300u);
    // same region, both fit one block
    EXPECT_EQ(s1.blockId, s2.blockId);
    EXPECT_EQ(a.liveBlocks(), 1u);
    EXPECT_EQ(a.liveSpans(), 2u);
    EXPECT_EQ(a.bytesUsed(), s1.capacity + s2.capacity);
    EXPECT_EQ(a.bytesCapacity(), kTestBlock);
    EXPECT_EQ(a.blockHandle(s1.blockId), 0);
}

TEST(ChunkArenaAllocatorTest, OversizedRequestFailsCleanly) {
    MockBackend be;
    ChunkArenaAllocator a(&be, kTestBlock, 4);
    EXPECT_FALSE(a.allocate(1, kTestBlock + 1).valid());
    EXPECT_EQ(a.liveBlocks(), 0u);
}

TEST(ChunkArenaAllocatorTest, BackendFailurePropagates) {
    MockBackend be;
    be.failNext = true;
    ChunkArenaAllocator a(&be, kTestBlock, 4);
    EXPECT_FALSE(a.allocate(1, 128).valid());
}

TEST(ChunkArenaAllocatorTest, RetiredBytesNotReusedBeforeMargin) {
    MockBackend be;
    ChunkArenaAllocator a(&be, kTestBlock, 100);
    // Fill the block completely with aligned spans.
    std::vector<ArenaSpan> spans;
    for (int i = 0; i < int(kTestBlock / ChunkArenaAllocator::kAlignment); ++i) {
        ArenaSpan s = a.allocate(7, ChunkArenaAllocator::kAlignment);
        ASSERT_TRUE(s.valid());
        spans.push_back(s);
    }
    EXPECT_EQ(a.liveBlocks(), 1u);

    // Retire one span. Its bytes must NOT be reused until kRetireMargin ticks pass:
    // an allocation in the meantime must come from a NEW block, not the freed hole.
    const ArenaSpan freed = spans.back();
    spans.pop_back();
    a.retire(freed);
    for (uint32_t t = 0; t < ChunkArenaAllocator::kRetireMargin; ++t) {
        ArenaSpan s = a.allocate(7, ChunkArenaAllocator::kAlignment);
        ASSERT_TRUE(s.valid());
        const bool reusedFreedBytes =
            s.blockId == freed.blockId && s.offset == freed.offset;
        EXPECT_FALSE(reusedFreedBytes) << "freed bytes handed out at tick +" << t;
        a.retire(s);  // keep the hole under contention every tick
        a.tick();
    }
    // Margin elapsed: the original hole is now legal to hand out again.
    a.tick();
    ArenaSpan s = a.allocate(7, ChunkArenaAllocator::kAlignment);
    ASSERT_TRUE(s.valid());
    EXPECT_EQ(s.blockId, freed.blockId);
}

TEST(ChunkArenaAllocatorTest, AdjacentRetiredSpansCoalesce) {
    MockBackend be;
    ChunkArenaAllocator a(&be, kTestBlock, 100);
    ArenaSpan s1 = a.allocate(1, 256);
    ArenaSpan s2 = a.allocate(1, 256);
    ArenaSpan s3 = a.allocate(1, 256);
    ASSERT_TRUE(s1.valid() && s2.valid() && s3.valid());
    ASSERT_EQ(s1.blockId, s2.blockId);
    // Fill the remainder so a 512-byte allocation cannot fit anywhere else in
    // this block, and record how many blocks exist now.
    std::vector<ArenaSpan> fillers;
    for (;;) {
        ArenaSpan f = a.allocate(1, 256);
        ASSERT_TRUE(f.valid());
        if (f.blockId != s1.blockId) { a.retire(f); break; }  // block full
        fillers.push_back(f);
    }
    const size_t blocksBefore = a.liveBlocks();

    a.retire(s1);
    a.retire(s2);
    for (uint32_t t = 0; t <= ChunkArenaAllocator::kRetireMargin; ++t) a.tick();

    // The two adjacent 256-byte holes must have coalesced into one 512-byte range.
    ArenaSpan big = a.allocate(1, 512);
    ASSERT_TRUE(big.valid());
    EXPECT_EQ(big.blockId, s1.blockId) << "coalesced hole not used";
    EXPECT_EQ(big.offset, std::min(s1.offset, s2.offset));
    EXPECT_EQ(a.liveBlocks(), blocksBefore) << "grew a block instead of coalescing";
    (void)s3;
}

TEST(ChunkArenaAllocatorTest, OpensSecondBlockWhenFullAndChainsRegion) {
    MockBackend be;
    ChunkArenaAllocator a(&be, kTestBlock, 100);
    // Exhaust block 0.
    const int spansPerBlock = int(kTestBlock / 512);
    for (int i = 0; i < spansPerBlock; ++i) ASSERT_TRUE(a.allocate(9, 512).valid());
    EXPECT_EQ(a.liveBlocks(), 1u);
    // Next allocation in the SAME region opens a second block.
    ArenaSpan s = a.allocate(9, 512);
    ASSERT_TRUE(s.valid());
    EXPECT_EQ(a.liveBlocks(), 2u);
}

TEST(ChunkArenaAllocatorTest, RegionsDoNotShareBlocks) {
    MockBackend be;
    ChunkArenaAllocator a(&be, kTestBlock, 100);
    ArenaSpan r1 = a.allocate(1, 256);
    ArenaSpan r2 = a.allocate(2, 256);
    ASSERT_TRUE(r1.valid() && r2.valid());
    EXPECT_NE(r1.blockId, r2.blockId);
    EXPECT_EQ(a.liveBlocks(), 2u);
}

TEST(ChunkArenaAllocatorTest, EmptyBlockReleasedAfterRetireWindow) {
    MockBackend be;
    const uint32_t kEmptyTicks = 5;
    ChunkArenaAllocator a(&be, kTestBlock, kEmptyTicks);
    ArenaSpan s = a.allocate(3, 256);
    ASSERT_TRUE(s.valid());
    EXPECT_EQ(be.live.size(), 1u);
    a.retire(s);
    // Span margin + empty window: block must be destroyed via the backend.
    for (uint32_t t = 0; t < ChunkArenaAllocator::kRetireMargin + kEmptyTicks + 1; ++t)
        a.tick();
    EXPECT_EQ(be.live.size(), 0u) << "empty block never released";
    EXPECT_EQ(a.liveBlocks(), 0u);
    EXPECT_EQ(a.bytesCapacity(), 0u);
    // Allocating again after release still works (fresh block).
    EXPECT_TRUE(a.allocate(3, 256).valid());
}

TEST(ChunkArenaAllocatorTest, ChurnKeepsInvariants) {
    // Deterministic alloc/retire churn: live spans never overlap, accounting exact.
    MockBackend be;
    ChunkArenaAllocator a(&be, kTestBlock, 8);
    SpanTracker tracker;
    uint32_t rng = 0xC0FFEEu;
    auto nextRand = [&rng]() { rng = rng * 1664525u + 1013904223u; return rng; };

    size_t expectedUsed = 0;
    for (int step = 0; step < 2000; ++step) {
        const bool doAlloc = tracker.count() < 4 || (nextRand() % 3) != 0;
        if (doAlloc) {
            const size_t bytes = 64 + nextRand() % 700;
            ArenaSpan s = a.allocate(nextRand() % 4, bytes);
            ASSERT_TRUE(s.valid()) << "step " << step;
            tracker.add(s);
            if (::testing::Test::HasFatalFailure()) return;
            expectedUsed += s.capacity;
        } else {
            ArenaSpan s = tracker.take(nextRand() % tracker.count());
            expectedUsed -= s.capacity;
            a.retire(s);
        }
        EXPECT_EQ(a.bytesUsed(), expectedUsed) << "step " << step;
        EXPECT_EQ(a.liveSpans(), tracker.count()) << "step " << step;
        a.tick();
    }
    // Capacity accounting: every live block's size is counted exactly once.
    EXPECT_EQ(a.bytesCapacity(), be.live.size() * kTestBlock);
    EXPECT_EQ(a.liveBlocks(), be.live.size());
}
