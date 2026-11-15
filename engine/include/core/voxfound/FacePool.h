#pragma once
// docs/VoxelFoundationPlan.md §4.6 item 10, F2.17 -- the face pool: per chunk, spans of one region arena.
//
// The pool IMAGE is a Preview::Level: one face array with each chunk's faces in its own span (slack after them, gaps
// between spans), and two cluster arrays (cell clusters, direction clusters) with each chunk's clusters in its own
// span of slots; unused slots are clusters with count 0 (voxfound_cull.comp never draws them). Spans come from
// Graphics::ChunkArenaAllocator (the region arena: aligned spans, a retire queue before bytes are reused), one block
// per array, so the whole array is ONE device buffer and a chunk's span is a stable offset into it.
// layout() places every chunk. update() rewrites only the listed chunks: in place when the new count fits the span;
// else a new span, the old one retired and its cluster slots zeroed in the same update (nothing may still point at
// faces that another chunk can reuse). It returns the dirty index ranges: copying exactly those ranges from the new
// image over the previous one gives the new image (the renderer's partial upload). A block that cannot fit = a full
// layout at 1.5x (PoolDirty::full).
#include <memory>
#include <vector>
#include "core/voxfound/Preview.h"
#include "graphics/ChunkArenaAllocator.h"

namespace Phyxel::Core::VoxFound {

/// One chunk's lists. Cluster firstFace values are relative to the chunk's own faces (0-based).
struct PoolChunk {
    const std::vector<PackedFace>* faces = nullptr;
    std::vector<PreviewCluster> clusters, clustersDir;
};
// PoolRange / PoolDirty (what changed in the image since the previous call: full = everything, else these index
// ranges) live in Preview.h: the preview carries them to the renderer.

class FacePool {
public:
    struct Spans {
        uint32_t faceFirst = 0, faceCap = 0;   // in faces
        uint32_t clusterFirst = 0, clusterCap = 0, dirFirst = 0, dirCap = 0;   // in slots
    };
    FacePool();
    ~FacePool();
    FacePool(const FacePool&) = delete;
    FacePool& operator=(const FacePool&) = delete;

    /// Places every chunk (capacity = live x grow, then slack per span); L is rewritten whole. d.full = true.
    void layout(const std::vector<PoolChunk>& in, Preview::Level& L, PoolDirty& d, double grow = 1.3);
    /// Rewrites the chunks in `changed` only (L must be the image of the previous layout/update of this pool).
    void update(const std::vector<PoolChunk>& in, const std::vector<size_t>& changed, Preview::Level& L, PoolDirty& d);

    bool laidOut() const { return !m_spans.empty(); }
    const Spans& spans(size_t i) const { return m_spans[i]; }
    size_t faceCapacity() const { return m_faceCap; }
    size_t clusterCapacity() const { return m_clusterCap; }
    size_t dirCapacity() const { return m_dirCap; }
    uint32_t layouts() const { return m_layouts; }   // full layouts so far (the first + every overflow)
    uint32_t moves() const { return m_moves; }       // spans that moved in update()

private:
    struct Arena;
    void write(const std::vector<PoolChunk>& in, size_t i, Preview::Level& L);   // chunk i's spans into L
    std::unique_ptr<Arena> m_faces, m_clusters, m_dir;
    std::vector<Spans> m_spans;
    std::vector<Graphics::ArenaSpan> m_faceSpan, m_clusterSpan, m_dirSpan;
    size_t m_faceCap = 0, m_clusterCap = 0, m_dirCap = 0;
    uint32_t m_layouts = 0, m_moves = 0;
    double m_grow = 1.3;
};

}  // namespace Phyxel::Core::VoxFound
