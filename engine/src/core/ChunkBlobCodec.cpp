#include "core/ChunkBlobCodec.h"
#include "core/Chunk.h"
#include "core/Cube.h"
#include "core/Subcube.h"
#include "core/Microcube.h"

#include <cstring>
#include <string>
#include <unordered_map>

namespace Phyxel {

namespace {

constexpr size_t kCubesPerChunk = 32 * 32 * 32;
constexpr uint8_t kFlagWideIndices = 0x01; // palette indices are u16
constexpr uint8_t kStateHasTint = 0x80;    // stateFlags bit 7
constexpr uint8_t kStateMask = 0x7F;
constexpr uint32_t kNoTint = 0xFFFFFFu;

// index = z + y*32 + x*1024 (the chunk's canonical z-minor order)
inline uint16_t localToIndex(const glm::ivec3& p) {
    return static_cast<uint16_t>(p.z + p.y * 32 + p.x * 1024);
}
inline glm::ivec3 indexToLocal(uint32_t i) {
    return glm::ivec3(static_cast<int>(i / 1024),
                      static_cast<int>((i % 1024) / 32),
                      static_cast<int>(i % 32));
}
inline bool inChunkBounds(const glm::ivec3& p) {
    return p.x >= 0 && p.x < 32 && p.y >= 0 && p.y < 32 && p.z >= 0 && p.z < 32;
}

// --- byte-level writer/reader (little-endian) ---

struct Writer {
    std::vector<uint8_t>& out;
    void u8(uint8_t v) { out.push_back(v); }
    void u16(uint16_t v) {
        out.push_back(static_cast<uint8_t>(v));
        out.push_back(static_cast<uint8_t>(v >> 8));
    }
    void u32(uint32_t v) {
        out.push_back(static_cast<uint8_t>(v));
        out.push_back(static_cast<uint8_t>(v >> 8));
        out.push_back(static_cast<uint8_t>(v >> 16));
        out.push_back(static_cast<uint8_t>(v >> 24));
    }
    void bytes(const void* data, size_t n) {
        const auto* b = static_cast<const uint8_t*>(data);
        out.insert(out.end(), b, b + n);
    }
};

struct Reader {
    const uint8_t* p;
    size_t remaining;
    bool ok = true;

    bool need(size_t n) {
        if (remaining < n) { ok = false; return false; }
        return true;
    }
    uint8_t u8() {
        if (!need(1)) return 0;
        uint8_t v = *p;
        p += 1; remaining -= 1;
        return v;
    }
    uint16_t u16() {
        if (!need(2)) return 0;
        uint16_t v = static_cast<uint16_t>(p[0] | (p[1] << 8));
        p += 2; remaining -= 2;
        return v;
    }
    uint32_t u32() {
        if (!need(4)) return 0;
        uint32_t v = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                     (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
        p += 4; remaining -= 4;
        return v;
    }
    bool str(std::string& out, size_t n) {
        if (!need(n)) return false;
        out.assign(reinterpret_cast<const char*>(p), n);
        p += n; remaining -= n;
        return true;
    }
    float f32() {
        // Bit-exact via the u32 path (little-endian, like every other field) — the fractional
        // water surface must round-trip without quantising.
        uint32_t bits = u32();
        float v;
        static_assert(sizeof(v) == sizeof(bits), "f32 codec assumes 32-bit float");
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }
};

inline void writeF32(Writer& w, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    w.u32(bits);
}

// Palette builder: entry 0 is always air (empty name).
struct PaletteBuilder {
    std::vector<std::string> names{std::string()};
    std::unordered_map<std::string, uint32_t> lookup;

    uint32_t indexOf(const std::string& name) {
        auto it = lookup.find(name);
        if (it != lookup.end()) return it->second;
        uint32_t idx = static_cast<uint32_t>(names.size());
        names.push_back(name);
        lookup.emplace(name, idx);
        return idx;
    }
};

struct SubEntry {
    uint16_t cubeIdx;
    uint8_t subIdx;
    uint8_t microIdx; // unused for subcubes
    uint32_t paletteIdx;
    uint8_t state;
    uint32_t tint;
};

void writePaletteIndex(Writer& w, uint32_t idx, bool wide) {
    if (wide) w.u16(static_cast<uint16_t>(idx));
    else w.u8(static_cast<uint8_t>(idx));
}

uint32_t readPaletteIndex(Reader& r, bool wide) {
    return wide ? r.u16() : r.u8();
}

void writeStateAndTint(Writer& w, uint8_t state, uint32_t tint) {
    uint8_t stateFlags = static_cast<uint8_t>(state & kStateMask);
    bool hasTint = (tint & 0xFFFFFFu) != kNoTint;
    if (hasTint) stateFlags |= kStateHasTint;
    w.u8(stateFlags);
    if (hasTint) {
        w.u8(static_cast<uint8_t>(tint >> 16));
        w.u8(static_cast<uint8_t>(tint >> 8));
        w.u8(static_cast<uint8_t>(tint));
    }
}

void readStateAndTint(Reader& r, uint8_t& state, uint32_t& tint) {
    uint8_t stateFlags = r.u8();
    state = stateFlags & kStateMask;
    if (stateFlags & kStateHasTint) {
        uint32_t rr = r.u8(), gg = r.u8(), bb = r.u8();
        tint = (rr << 16) | (gg << 8) | bb;
    } else {
        tint = kNoTint;
    }
}

} // namespace

std::vector<uint8_t> ChunkBlobCodec::encode(const Chunk& chunk, Counts* outCounts) {
    PaletteBuilder palette;
    const glm::ivec3 worldOrigin = chunk.getWorldOrigin();

    // --- cube section: palette indices in canonical order, RLE'd ---
    struct Run { uint16_t length; uint32_t idx; };
    std::vector<Run> runs;
    uint32_t cubeCount = 0;
    {
        // 4.2b hybrid read: the palette store is the authority for static voxels; a materialized
        // overlay Cube (getCubeAtIndex — the RAW slot read) wins where present, so unsynced
        // direct Cube mutations still save correctly.
        const ChunkVoxelStore& store = chunk.getVoxelStore();
        uint32_t runIdx = 0;
        uint32_t runLen = 0;
        for (size_t i = 0; i < kCubesPerChunk; ++i) {
            const Cube* cube = chunk.getCubeAtIndex(i);
            uint32_t idx = 0;
            if (cube ? cube->isVisible() : store.visible(i)) {
                idx = palette.indexOf(cube ? cube->getMaterialName() : store.material(i));
                ++cubeCount;
            }
            if (runLen > 0 && idx == runIdx && runLen < 0xFFFF) {
                ++runLen;
            } else {
                if (runLen > 0) runs.push_back({static_cast<uint16_t>(runLen), runIdx});
                runIdx = idx;
                runLen = 1;
            }
        }
        runs.push_back({static_cast<uint16_t>(runLen), runIdx});
    }

    // --- subcube / microcube sections (sparse; only visible, in-bounds) ---
    std::vector<SubEntry> subs;
    for (const auto& subcube : chunk.getStaticSubcubes()) {
        if (!subcube || !subcube->isVisible()) continue;
        glm::ivec3 parentLocal = subcube->getPosition() - worldOrigin;
        if (!inChunkBounds(parentLocal)) continue;
        const glm::ivec3& sp = subcube->getLocalPosition();
        subs.push_back({localToIndex(parentLocal),
                        static_cast<uint8_t>(sp.x + sp.y * 3 + sp.z * 9), 0,
                        palette.indexOf(subcube->getMaterialName()),
                        subcube->getState(), subcube->getTint()});
    }

    std::vector<SubEntry> micros;
    for (const auto& micro : chunk.getStaticMicrocubes()) {
        if (!micro || !micro->isVisible()) continue;
        glm::ivec3 parentLocal = micro->getParentCubePosition() - worldOrigin;
        if (!inChunkBounds(parentLocal)) continue;
        const glm::ivec3& sp = micro->getSubcubeLocalPosition();
        const glm::ivec3& mp = micro->getMicrocubeLocalPosition();
        micros.push_back({localToIndex(parentLocal),
                          static_cast<uint8_t>(sp.x + sp.y * 3 + sp.z * 9),
                          static_cast<uint8_t>(mp.x + mp.y * 3 + mp.z * 9),
                          palette.indexOf(micro->getMaterialName()),
                          micro->getState(), micro->getTint()});
    }

    // --- serialize ---
    const bool wide = palette.names.size() > 256;
    std::vector<uint8_t> blob;
    blob.reserve(64 + runs.size() * 3 + subs.size() * 8 + micros.size() * 9);
    Writer w{blob};

    w.u32(kMagic);
    w.u8(kCodecVersion);
    w.u8(wide ? kFlagWideIndices : 0);
    w.u16(static_cast<uint16_t>(palette.names.size()));
    w.u32(cubeCount);
    w.u32(static_cast<uint32_t>(subs.size()));
    w.u32(static_cast<uint32_t>(micros.size()));

    for (const auto& name : palette.names) {
        w.u8(static_cast<uint8_t>(name.size() > 255 ? 255 : name.size()));
        w.bytes(name.data(), name.size() > 255 ? 255 : name.size());
    }

    w.u32(static_cast<uint32_t>(runs.size()));
    for (const auto& run : runs) {
        w.u16(run.length);
        writePaletteIndex(w, run.idx, wide);
    }

    for (const auto& e : subs) {
        w.u16(e.cubeIdx);
        w.u8(e.subIdx);
        writePaletteIndex(w, e.paletteIdx, wide);
        writeStateAndTint(w, e.state, e.tint);
    }
    for (const auto& e : micros) {
        w.u16(e.cubeIdx);
        w.u8(e.subIdx);
        w.u8(e.microIdx);
        writePaletteIndex(w, e.paletteIdx, wide);
        writeStateAndTint(w, e.state, e.tint);
    }

    // --- water span section (v2; docs/Water.md §2 layer 1) ---
    const auto& spans = chunk.getWaterSpans();
    w.u32(static_cast<uint32_t>(spans.size()));
    for (const auto& s : spans) {
        w.u8(s.x);
        w.u8(s.z);
        writeF32(w, s.bottom);
        writeF32(w, s.top);
    }

    if (outCounts) {
        outCounts->cubes = cubeCount;
        outCounts->subcubes = static_cast<uint32_t>(subs.size());
        outCounts->microcubes = static_cast<uint32_t>(micros.size());
    }
    return blob;
}

bool ChunkBlobCodec::decode(const uint8_t* data, size_t size, Chunk& chunk,
                            Counts* outCounts) {
    if (!data) return false;
    Reader r{data, size};

    if (r.u32() != kMagic) return false;
    // v1 blobs (pre-water) load unchanged — spans simply stay empty. Anything newer than this
    // build's version is refused: guessing at an unknown layout is how blobs get misread.
    const uint8_t version = r.u8();
    if (version < kMinDecodeVersion || version > kCodecVersion) return false;
    const uint8_t flags = r.u8();
    const bool wide = (flags & kFlagWideIndices) != 0;
    const uint32_t paletteCount = r.u16();
    const uint32_t cubeCount = r.u32();
    const uint32_t subcubeCount = r.u32();
    const uint32_t microcubeCount = r.u32();
    if (!r.ok || paletteCount == 0) return false;

    std::vector<std::string> palette(paletteCount);
    for (uint32_t i = 0; i < paletteCount; ++i) {
        uint8_t len = r.u8();
        if (!r.ok || !r.str(palette[i], len)) return false;
    }

    // --- cube section ---
    const uint32_t runCount = r.u32();
    if (!r.ok) return false;
    size_t cell = 0;
    uint32_t decodedCubes = 0;
    for (uint32_t i = 0; i < runCount; ++i) {
        const uint32_t runLen = r.u16();
        const uint32_t idx = readPaletteIndex(r, wide);
        if (!r.ok || runLen == 0 || idx >= paletteCount) return false;
        if (cell + runLen > kCubesPerChunk) return false;
        if (idx != 0) {
            if (runLen == kCubesPerChunk) {
                // Whole chunk = one visible material: O(1) uniform fill (Phase 4.4). Keeps
                // DB-loaded buried chunks in the uniform representation instead of 32k addCubes.
                chunk.fillAllCubes(palette[idx]);
            } else {
                for (uint32_t j = 0; j < runLen; ++j) {
                    chunk.addCube(indexToLocal(static_cast<uint32_t>(cell + j)), palette[idx]);
                }
            }
            decodedCubes += runLen;
        }
        cell += runLen;
    }
    if (cell != kCubesPerChunk || decodedCubes != cubeCount) return false;

    // --- subcube section ---
    for (uint32_t i = 0; i < subcubeCount; ++i) {
        const uint16_t cubeIdx = r.u16();
        const uint8_t subIdx = r.u8();
        const uint32_t idx = readPaletteIndex(r, wide);
        uint8_t state;
        uint32_t tint;
        readStateAndTint(r, state, tint);
        if (!r.ok || cubeIdx >= kCubesPerChunk || subIdx >= 27 || idx >= paletteCount)
            return false;
        glm::ivec3 subPos(subIdx % 3, (subIdx / 3) % 3, subIdx / 9);
        chunk.addSubcube(indexToLocal(cubeIdx), subPos, palette[idx], tint, state);
    }

    // --- microcube section ---
    for (uint32_t i = 0; i < microcubeCount; ++i) {
        const uint16_t cubeIdx = r.u16();
        const uint8_t subIdx = r.u8();
        const uint8_t microIdx = r.u8();
        const uint32_t idx = readPaletteIndex(r, wide);
        uint8_t state;
        uint32_t tint;
        readStateAndTint(r, state, tint);
        if (!r.ok || cubeIdx >= kCubesPerChunk || subIdx >= 27 || microIdx >= 27 ||
            idx >= paletteCount)
            return false;
        glm::ivec3 subPos(subIdx % 3, (subIdx / 3) % 3, subIdx / 9);
        glm::ivec3 microPos(microIdx % 3, (microIdx / 3) % 3, microIdx / 9);
        chunk.addMicrocube(indexToLocal(cubeIdx), subPos, microPos, palette[idx], tint, state);
    }

    // --- water span section (v2 only; a v1 blob simply has none) ---
    if (version >= 2) {
        const uint32_t spanCount = r.u32();
        if (!r.ok) return false;
        // Cap before allocating: a corrupt count must not become a multi-GB reserve. A chunk has
        // 1,024 columns; even a future multi-run-per-column format stays far under 8 per column.
        if (spanCount > 32 * 32 * 8) return false;
        std::vector<Chunk::WaterSpanLocal> spans;
        spans.reserve(spanCount);
        uint32_t prevKey = 0;
        for (uint32_t i = 0; i < spanCount; ++i) {
            Chunk::WaterSpanLocal s;
            s.x = r.u8();
            s.z = r.u8();
            s.bottom = r.f32();
            s.top = r.f32();
            // Refuse malformed data outright — never clamp into something plausible. The bounds
            // mirror setWaterSpans' debug asserts so a bad blob fails HERE, not as an assert
            // deep in a release build. NaN fails these comparisons too (any NaN compare is
            // false), so a poisoned float cannot enter world data.
            if (!r.ok || s.x >= 32 || s.z >= 32 ||
                !(s.bottom >= 0.0f) || !(s.top <= 32.0f) || !(s.top > s.bottom))
                return false;
            const uint32_t key = (static_cast<uint32_t>(s.x) << 8) | s.z;
            // (x,z) order is part of the format; same-column runs must ascend by bottom (the
            // future cave-lake case). Mirrors setWaterSpans' contract so a decoded chunk can
            // never trip its debug assert.
            if (i > 0 && (key < prevKey ||
                          (key == prevKey && !(s.bottom > spans.back().bottom))))
                return false;
            prevKey = key;
            spans.push_back(s);
        }
        chunk.setWaterSpans(std::move(spans));
    }

    if (outCounts) {
        outCounts->cubes = cubeCount;
        outCounts->subcubes = subcubeCount;
        outCounts->microcubes = microcubeCount;
    }
    return true;
}

} // namespace Phyxel
