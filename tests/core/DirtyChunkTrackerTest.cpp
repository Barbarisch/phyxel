// DirtyChunkTracker — the remesh queue must survive streaming eviction.
//
// The queue was keyed by INDEX into the chunks vector; streaming eviction erases from
// that vector and shifts every later index, so a queued remesh silently retargeted to a
// DIFFERENT chunk and the marked chunk never meshed — resident with correct voxel data
// but invisible until the next edit re-marked it (user repro 2026-08-02: a settlement
// area "stuck in low poly", actually an unmeshed hole papered over by a far tile).
// The queue is now keyed by chunk COORDINATE, resolved to the live chunk at process time.

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "core/Chunk.h"
#include "core/DirtyChunkTracker.h"
#include "utils/CoordinateUtils.h"

using namespace Phyxel;

namespace {

struct Harness {
    std::vector<std::unique_ptr<Chunk>> chunks;
    std::vector<glm::ivec3> updated;   // chunk coords actually processed
    DirtyChunkTracker tracker;

    Harness() {
        tracker.setCallbacks(
            [this]() -> std::vector<std::unique_ptr<Chunk>>& { return chunks; },
            [this](size_t idx) {
                updated.push_back(
                    Utils::CoordinateUtils::worldToChunkCoord(chunks[idx]->getWorldOrigin()));
            },
            [this](Chunk* c) -> size_t {
                for (size_t i = 0; i < chunks.size(); ++i)
                    if (chunks[i].get() == c) return i;
                return SIZE_MAX;
            },
            [this](const glm::ivec3& coord) -> Chunk* {
                const glm::ivec3 origin = coord * 32;
                for (auto& c : chunks)
                    if (c->getWorldOrigin() == origin) return c.get();
                return nullptr;
            });
    }

    Chunk* add(const glm::ivec3& chunkCoord) {
        auto c = std::make_unique<Chunk>(chunkCoord * 32);
        chunks.push_back(std::move(c));
        return chunks.back().get();
    }
};

} // namespace

// THE regression: evicting an earlier chunk between mark and process must not retarget
// or drop the queued remesh.
TEST(DirtyChunkTrackerTest, QueuedRemeshSurvivesEvictionOfEarlierChunk) {
    Harness h;
    h.add(glm::ivec3(0, 0, 0));                  // index 0 — will be evicted
    Chunk* village = h.add(glm::ivec3(5, 1, 5)); // index 1 — the marked chunk

    h.tracker.markChunkForRemesh(village);

    // Streaming eviction: erase index 0 — village shifts to index 0.
    h.chunks.erase(h.chunks.begin());

    h.tracker.updateDirtyChunks();

    ASSERT_EQ(h.updated.size(), 1u)
        << "the queued remesh was dropped (or retargeted out of range) by the eviction";
    EXPECT_EQ(h.updated[0], glm::ivec3(5, 1, 5))
        << "the remesh ran on the WRONG chunk — index-keyed queue retargeted by eviction";
}

// An evicted chunk's own queued remesh must drop out silently, never hit another chunk.
TEST(DirtyChunkTrackerTest, EvictedChunksQueuedRemeshDropsOut) {
    Harness h;
    Chunk* doomed = h.add(glm::ivec3(1, 0, 1));
    h.add(glm::ivec3(2, 0, 2));

    h.tracker.markChunkForRemesh(doomed);
    h.chunks.erase(h.chunks.begin());   // evict the marked chunk itself

    h.tracker.updateDirtyChunks();
    EXPECT_TRUE(h.updated.empty())
        << "a remesh queued for an evicted chunk ran against a survivor";
}

// Idle-tier (cosmetic) marks get the same coordinate-keyed treatment.
TEST(DirtyChunkTrackerTest, IdleTierSurvivesEvictionToo) {
    Harness h;
    h.add(glm::ivec3(0, 0, 0));
    Chunk* neighbor = h.add(glm::ivec3(9, 0, 9));

    h.tracker.markChunkForRemeshIdle(neighbor);
    h.chunks.erase(h.chunks.begin());

    h.tracker.updateDirtyChunks();   // empty dirty queue -> promotes one idle chunk
    ASSERT_EQ(h.updated.size(), 1u);
    EXPECT_EQ(h.updated[0], glm::ivec3(9, 0, 9));
}


// G-181: a settle check waits for the dirty chunks NEAR a position. Chunk boxes are 32 wide,
// so from (16,16,16) chunk (1,0,0) is 16 u away and chunk (5,0,0) is 144 u away.
TEST(DirtyChunkTrackerTest, DirtyCountNearCountsOnlyChunksWithinTheRadius) {
    Harness h;
    Chunk* c0 = h.add({0, 0, 0});
    Chunk* c1 = h.add({1, 0, 0});
    Chunk* c5 = h.add({5, 0, 0});
    Chunk* cz = h.add({0, 0, 6});
    for (Chunk* c : {c0, c1, c5, cz}) h.tracker.markChunkForRemesh(c);
    ASSERT_EQ(h.tracker.getDirtyCount(), 4u);
    const glm::vec3 p(16.0f, 16.0f, 16.0f);
    EXPECT_EQ(h.tracker.getDirtyCountNear(p, 8.0f), 1u)   << "only the chunk the point is in";
    EXPECT_EQ(h.tracker.getDirtyCountNear(p, 40.0f), 2u)  << "(1,0,0) is 16 u away, (5,0,0) 144, (0,0,6) 176";
    EXPECT_EQ(h.tracker.getDirtyCountNear(p, 150.0f), 3u);
    EXPECT_EQ(h.tracker.getDirtyCountNear(p, 200.0f), 4u) << "wide enough: the global count";
    // draining the queue empties the near count too
    h.tracker.updateDirtyChunks();
    EXPECT_EQ(h.tracker.getDirtyCountNear(p, 200.0f), 0u);
}


TEST(DirtyChunkTrackerTest, DirtyCountInXZCountsChunksWhoseFootprintOverlapsTheRegion) {
    Harness h;
    for (const glm::ivec3& cc : {glm::ivec3{0, 0, 0}, glm::ivec3{1, 0, 0}, glm::ivec3{1, 3, 0}, glm::ivec3{4, 0, 0}, glm::ivec3{0, 0, 2}})
        h.tracker.markChunkForRemesh(h.add(cc));
    ASSERT_EQ(h.tracker.getDirtyCount(), 5u);
    // a region over x 10..50, z 5..20: chunks (0,0,0), (1,0,0) and (1,3,0) overlap it (any height)
    EXPECT_EQ(h.tracker.getDirtyCountInXZ(10.0f, 50.0f, 5.0f, 20.0f), 3u);
    // touching at the edge is not overlapping (half-open): x 32..64 vs a region ending at 32
    EXPECT_EQ(h.tracker.getDirtyCountInXZ(0.0f, 32.0f, 0.0f, 32.0f), 1u);
    EXPECT_EQ(h.tracker.getDirtyCountInXZ(-1000.0f, 1000.0f, -1000.0f, 1000.0f), 5u);
    EXPECT_EQ(h.tracker.getDirtyCountInXZ(200.0f, 300.0f, 0.0f, 32.0f), 0u);
}


// G-181: a budgeted drain meshes the chunks nearest the focus first, whatever order they were
// queued in. A tiny budget processes exactly one chunk per call ("always at least one, then
// bail once over budget"), so the processing order is observable.
TEST(DirtyChunkTrackerTest, BudgetedDrainServesTheChunksNearestTheFocusFirst) {
    Harness h;
    for (const glm::ivec3& cc : {glm::ivec3{5, 0, 0}, glm::ivec3{3, 0, 0}, glm::ivec3{0, 0, 0}, glm::ivec3{1, 0, 0}})
        h.tracker.markChunkForRemesh(h.add(cc));   // queued far-to-near, as a streamer would
    h.tracker.setFocus({16.0f, 16.0f, 16.0f});    // inside chunk (0,0,0)
    // a call that blows its budget is followed by a 2-frame backoff, so more calls than chunks
    for (int i = 0; i < 20 && h.updated.size() < 4; ++i) h.tracker.updateDirtyChunks(1e-6);
    ASSERT_EQ(h.updated.size(), 4u);
    EXPECT_EQ(h.updated[0], glm::ivec3(0, 0, 0));
    EXPECT_EQ(h.updated[1], glm::ivec3(1, 0, 0));
    EXPECT_EQ(h.updated[2], glm::ivec3(3, 0, 0));
    EXPECT_EQ(h.updated[3], glm::ivec3(5, 0, 0));
    EXPECT_EQ(h.tracker.getDirtyCount(), 0u);
    // without a focus the queue is FIFO (the pre-G-181 behaviour, kept for headless drains)
    Harness g;
    for (const glm::ivec3& cc : {glm::ivec3{5, 0, 0}, glm::ivec3{0, 0, 0}}) g.tracker.markChunkForRemesh(g.add(cc));
    g.tracker.updateDirtyChunks(1e-6);
    ASSERT_EQ(g.updated.size(), 1u);
    EXPECT_EQ(g.updated[0], glm::ivec3(5, 0, 0));
}


// G-181: a priority XZ region (the play region) drains before everything else, nearest-first
// inside each group - a region chunk 160 u away goes before a surround chunk in the next cell.
TEST(DirtyChunkTrackerTest, PriorityRegionDrainsBeforeNearerChunksOutsideIt) {
    Harness h;
    for (const glm::ivec3& cc : {glm::ivec3{1, 0, 0}, glm::ivec3{9, 0, 0}, glm::ivec3{7, 0, 0}, glm::ivec3{0, 0, 3}})
        h.tracker.markChunkForRemesh(h.add(cc));
    h.tracker.setFocus({16.0f, 16.0f, 16.0f});                // in chunk (0,0,0)
    h.tracker.setPriorityXZ(200.0f, 330.0f, 0.0f, 32.0f);      // chunks x 6..10 at z 0 -> (7,0,0) and (9,0,0)
    // a call that blows its budget is followed by a 2-frame backoff, so more calls than chunks
    for (int i = 0; i < 20 && h.updated.size() < 4; ++i) h.tracker.updateDirtyChunks(1e-6);
    ASSERT_EQ(h.updated.size(), 4u);
    EXPECT_EQ(h.updated[0], glm::ivec3(7, 0, 0)) << "region first, nearer region chunk first";
    EXPECT_EQ(h.updated[1], glm::ivec3(9, 0, 0));
    EXPECT_EQ(h.updated[2], glm::ivec3(1, 0, 0)) << "then the rest by distance";
    EXPECT_EQ(h.updated[3], glm::ivec3(0, 0, 3));
}


// The exact-coordinate form: chunks above/below the block's footprint are NOT the block.
TEST(DirtyChunkTrackerTest, PriorityCoordsSelectExactChunksNotTheirColumn) {
    Harness h;
    Chunk* inBlock = h.add({3, 0, 0});
    Chunk* below   = h.add({3, -1, 0});   // same XZ footprint, one level down (the streamed underground)
    Chunk* near0   = h.add({0, 0, 0});
    for (Chunk* c : {below, near0, inBlock}) h.tracker.markChunkForRemesh(c);
    h.tracker.setFocus({16.0f, 16.0f, 16.0f});
    h.tracker.setPriorityCoords({glm::ivec3(3, 0, 0), glm::ivec3(4, 0, 0)});
    EXPECT_EQ(h.tracker.getDirtyCountInPriority(), 1u) << "only (3,0,0) is in the block";
    h.tracker.updateDirtyChunks(1e-6);
    ASSERT_EQ(h.updated.size(), 1u);
    EXPECT_EQ(h.updated[0], glm::ivec3(3, 0, 0)) << "the block chunk goes first, not the nearer (0,0,0) nor the chunk under it";
    EXPECT_EQ(h.tracker.getDirtyCountInPriority(), 0u);
    EXPECT_EQ(h.tracker.getDirtyCount(), 2u);
}
