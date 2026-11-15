// docs/VoxelFoundationPlan.md §4.6 item 10, F2.17 -- see FacePool.h.
#include "core/voxfound/FacePool.h"

#include <algorithm>
#include <cstdint>

namespace Phyxel::Core::VoxFound {

namespace {
// The arena's blocks are index ranges of the pool image, not GPU memory: the backend only hands out ids.
struct CountingBackend : Graphics::ArenaBlockBackend {
    int next = 0;
    int createBlock(size_t) override { return next++; }
    void destroyBlock(int) override {}
};
constexpr size_t kFaceUnit = sizeof(PackedFace);   // 8 B
constexpr size_t kSlotUnit = 32;                   // a cluster slot (ClusterCullGpu's record is 32 B)
static_assert(Graphics::ChunkArenaAllocator::kAlignment % kFaceUnit == 0, "face spans must start on a face");
static_assert(Graphics::ChunkArenaAllocator::kAlignment % kSlotUnit == 0, "slot spans must start on a slot");

/// Units a span of `count` asks for: the count plus a quarter (at least one) of slack.
size_t want(size_t count) { return count + std::max<size_t>(count / 4, 1); }
size_t roundUp(size_t bytes) {
    const size_t a = Graphics::ChunkArenaAllocator::kAlignment;
    return (bytes + a - 1) / a * a;
}
}  // namespace

struct FacePool::Arena {
    CountingBackend backend;
    Graphics::ChunkArenaAllocator alloc;
    size_t unit;
    size_t blockBytes;
    uint32_t block = Graphics::ArenaSpan::kInvalid;
    Arena(size_t bytes, size_t u) : alloc(&backend, bytes, UINT32_MAX), unit(u), blockBytes(bytes) {}
    /// A span of >= units in the arena's ONE block; false when it does not fit there (the pool overflowed).
    bool get(size_t units, Graphics::ArenaSpan& s) {
        s = alloc.allocate(0, std::max<size_t>(units, 1) * unit);
        if (!s.valid()) return false;
        if (block == Graphics::ArenaSpan::kInvalid) block = s.blockId;
        if (s.blockId != block) {   // a second block = the image would not be one buffer
            alloc.retire(s);
            s = Graphics::ArenaSpan{};
            return false;
        }
        return true;
    }
    uint32_t first(const Graphics::ArenaSpan& s) const { return uint32_t(s.offset / unit); }
    uint32_t cap(const Graphics::ArenaSpan& s) const { return uint32_t(s.capacity / unit); }
    size_t capacity() const { return blockBytes / unit; }
};

FacePool::FacePool() = default;
FacePool::~FacePool() = default;

namespace {
void writeSlots(std::vector<PreviewCluster>& arr, const std::vector<PreviewCluster>& mine, uint32_t first, uint32_t cap,
                uint32_t faceFirst) {
    for (uint32_t k = 0; k < cap; ++k) {
        if (k < mine.size()) {
            PreviewCluster c = mine[k];
            c.firstFace += faceFirst;
            arr[first + k] = c;
        } else {
            arr[first + k] = PreviewCluster{};   // count 0: never drawn (voxfound_cull.comp)
        }
    }
}
}  // namespace

void FacePool::layout(const std::vector<PoolChunk>& in, Preview::Level& L, PoolDirty& d, double grow) {
    for (double g = std::max(grow, 1.0);; g *= 1.5) {
        size_t f = 0, c = 0, r = 0;
        for (const PoolChunk& k : in) {
            f += roundUp(want(k.faces ? k.faces->size() : 0) * kFaceUnit);
            c += roundUp(want(k.clusters.size()) * kSlotUnit);
            r += roundUp(want(k.clustersDir.size()) * kSlotUnit);
        }
        m_faces = std::make_unique<Arena>(roundUp(size_t(double(f) * g)), kFaceUnit);
        m_clusters = std::make_unique<Arena>(roundUp(size_t(double(c) * g)), kSlotUnit);
        m_dir = std::make_unique<Arena>(roundUp(size_t(double(r) * g)), kSlotUnit);
        m_faceSpan.assign(in.size(), {});
        m_clusterSpan.assign(in.size(), {});
        m_dirSpan.assign(in.size(), {});
        bool ok = true;
        for (size_t i = 0; i < in.size() && ok; ++i)
            ok = m_faces->get(want(in[i].faces ? in[i].faces->size() : 0), m_faceSpan[i]) &&
                 m_clusters->get(want(in[i].clusters.size()), m_clusterSpan[i]) &&
                 m_dir->get(want(in[i].clustersDir.size()), m_dirSpan[i]);
        if (!ok) continue;   // alignment padding beat the estimate: grow and place again
        m_grow = g;
        break;
    }
    m_faceCap = m_faces->capacity();
    m_clusterCap = m_clusters->capacity();
    m_dirCap = m_dir->capacity();
    L.faces.assign(m_faceCap, PackedFace{});
    L.clusters.assign(m_clusterCap, PreviewCluster{});
    L.clustersDir.assign(m_dirCap, PreviewCluster{});
    L.firstFace.assign(in.size(), 0u);
    L.faceCount.assign(in.size(), 0u);
    L.leafCount.assign(in.size(), 0u);
    m_spans.assign(in.size(), Spans{});
    for (size_t i = 0; i < in.size(); ++i) write(in, i, L);
    ++m_layouts;
    d = PoolDirty{};
    d.full = true;
}

void FacePool::write(const std::vector<PoolChunk>& in, size_t i, Preview::Level& L) {
    Spans& sp = m_spans[i];
    sp.faceFirst = m_faces->first(m_faceSpan[i]);
    sp.faceCap = m_faces->cap(m_faceSpan[i]);
    sp.clusterFirst = m_clusters->first(m_clusterSpan[i]);
    sp.clusterCap = m_clusters->cap(m_clusterSpan[i]);
    sp.dirFirst = m_dir->first(m_dirSpan[i]);
    sp.dirCap = m_dir->cap(m_dirSpan[i]);
    const size_t n = in[i].faces ? in[i].faces->size() : 0;
    if (n) std::copy(in[i].faces->begin(), in[i].faces->end(), L.faces.begin() + sp.faceFirst);
    writeSlots(L.clusters, in[i].clusters, sp.clusterFirst, sp.clusterCap, sp.faceFirst);
    writeSlots(L.clustersDir, in[i].clustersDir, sp.dirFirst, sp.dirCap, sp.faceFirst);
    L.firstFace[i] = sp.faceFirst;
    L.faceCount[i] = uint32_t(n);
}

void FacePool::update(const std::vector<PoolChunk>& in, const std::vector<size_t>& changed, Preview::Level& L,
                      PoolDirty& d) {
    if (!laidOut() || m_spans.size() != in.size() || L.faces.size() != m_faceCap || L.clusters.size() != m_clusterCap ||
        L.clustersDir.size() != m_dirCap) {
        layout(in, L, d, m_grow);
        return;
    }
    d = PoolDirty{};
    std::vector<size_t> todo(changed);
    std::sort(todo.begin(), todo.end());
    todo.erase(std::unique(todo.begin(), todo.end()), todo.end());
    for (size_t i : todo) {
        if (i >= in.size()) continue;
        const size_t n = in[i].faces ? in[i].faces->size() : 0;
        // a span that no longer fits moves; a moved CLUSTER span's old slots are zeroed in this same update, so no
        // cluster can draw the faces of a retired span once another chunk reuses it
        auto regrow = [&](Arena& a, Graphics::ArenaSpan& s, size_t count, std::vector<PreviewCluster>* slots,
                          std::vector<PoolRange>* dirty) {
            if (count <= a.cap(s)) return true;
            if (slots) {
                const uint32_t f = a.first(s), c = a.cap(s);
                for (uint32_t k = 0; k < c; ++k) (*slots)[f + k] = PreviewCluster{};
                dirty->push_back({f, c});
            }
            a.alloc.retire(s);
            ++m_moves;
            return a.get(want(count), s);
        };
        if (!regrow(*m_faces, m_faceSpan[i], n, nullptr, nullptr) ||
            !regrow(*m_clusters, m_clusterSpan[i], in[i].clusters.size(), &L.clusters, &d.clusters) ||
            !regrow(*m_dir, m_dirSpan[i], in[i].clustersDir.size(), &L.clustersDir, &d.clustersDir)) {
            layout(in, L, d, m_grow * 1.5);   // the block is full: place everything again, bigger
            return;
        }
        write(in, i, L);
        const Spans& sp = m_spans[i];
        if (n) d.faces.push_back({sp.faceFirst, uint32_t(n)});
        d.clusters.push_back({sp.clusterFirst, sp.clusterCap});
        d.clustersDir.push_back({sp.dirFirst, sp.dirCap});
    }
    m_faces->alloc.tick();
    m_clusters->alloc.tick();
    m_dir->alloc.tick();
}

}  // namespace Phyxel::Core::VoxFound
