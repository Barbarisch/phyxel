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
