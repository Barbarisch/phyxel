#include "core/LodBlobCodec.h"

#include <cstring>

namespace Phyxel {
namespace Core {

namespace {

constexpr uint8_t kFlagWidePalette = 0x01;   // header flags bit0
constexpr uint8_t kCellSolid       = 0x01;   // cell flags bit0
constexpr uint8_t kCellOpening     = 0x02;   // cell flags bit1

struct Writer {
    std::vector<uint8_t>& b;
    void u8v(uint8_t v)  { b.push_back(v); }
    void u16v(uint16_t v) { b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8)); }
    void u32v(uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i))); }
    void u64v(uint64_t v) { for (int i = 0; i < 8; ++i) b.push_back(uint8_t(v >> (8 * i))); }
};

struct Reader {
    const uint8_t* p; size_t remaining; bool ok = true;
    bool need(size_t n) { if (remaining < n) { ok = false; return false; } return true; }
    uint8_t u8v()  { if (!need(1)) return 0; uint8_t v = *p++; --remaining; return v; }
    uint16_t u16v() {
        if (!need(2)) return 0;
        uint16_t v = uint16_t(p[0]) | (uint16_t(p[1]) << 8);
        p += 2; remaining -= 2; return v;
    }
    uint32_t u32v() {
        if (!need(4)) return 0;
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= uint32_t(p[i]) << (8 * i);
        p += 4; remaining -= 4; return v;
    }
    uint64_t u64v() {
        if (!need(8)) return 0;
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v |= uint64_t(p[i]) << (8 * i);
        p += 8; remaining -= 8; return v;
    }
};

/// Two cells share a run only if EVERY persisted field matches. Comparing just `solid` would
/// silently merge cells with different materials or opening volumes and quietly corrupt the
/// decode -- the kind of loss that only shows up as wrong-coloured terrain much later.
bool sameCell(const LodCell& a, const LodCell& b) {
    return a.coverage == b.coverage &&
           a.bulkMaterial == b.bulkMaterial &&
           a.skinMaterial == b.skinMaterial &&
           a.preserveOpening == b.preserveOpening &&
           a.openingCoverage == b.openingCoverage;
}

} // namespace

std::vector<uint8_t> LodBlobCodec::encode(const LodVolume& volume,
                                          const std::vector<std::string>& palette) {
    std::vector<uint8_t> out;
    Writer w{out};

    const glm::ivec3 d = volume.dim();
    const bool wide = palette.size() > 255;

    w.u32v(kMagic);
    w.u8v(kCodecVersion);
    w.u8v(static_cast<uint8_t>(volume.level()));
    w.u8v(wide ? kFlagWidePalette : 0);
    w.u16v(static_cast<uint16_t>(palette.size()));
    w.u16v(static_cast<uint16_t>(d.x));
    w.u16v(static_cast<uint16_t>(d.y));
    w.u16v(static_cast<uint16_t>(d.z));
    for (const std::string& name : palette) {
        const size_t n = name.size() > 255 ? 255 : name.size();
        w.u8v(static_cast<uint8_t>(n));
        out.insert(out.end(), name.begin(), name.begin() + n);
    }

    auto writeIdx = [&](uint16_t v) { if (wide) w.u16v(v); else w.u8v(static_cast<uint8_t>(v)); };

    // Collect runs first so the count can be written before them.
    struct Run { uint16_t len; LodCell cell; };
    std::vector<Run> runs;
    const size_t total = size_t(d.x) * size_t(d.y) * size_t(d.z);
    if (total > 0) {
        // Canonical z-minor order, matching LodVolume::index and the engine's convention.
        auto cellAt = [&](size_t i) -> const LodCell& {
            const int x = int(i / (size_t(d.z) * size_t(d.y)));
            const int y = int((i / size_t(d.z)) % size_t(d.y));
            const int z = int(i % size_t(d.z));
            return volume.at(x, y, z);
        };
        LodCell cur = cellAt(0);
        uint32_t len = 1;
        for (size_t i = 1; i < total; ++i) {
            const LodCell& c = cellAt(i);
            if (len < 0xFFFFu && sameCell(c, cur)) { ++len; continue; }
            runs.push_back({static_cast<uint16_t>(len), cur});
            cur = c; len = 1;
        }
        runs.push_back({static_cast<uint16_t>(len), cur});
    }

    w.u32v(static_cast<uint32_t>(runs.size()));
    for (const Run& r : runs) {
        uint8_t flags = 0;
        if (r.cell.solid()) flags |= kCellSolid;
        if (r.cell.preserveOpening) flags |= kCellOpening;
        w.u16v(r.len);
        w.u8v(flags);
        if (flags & kCellSolid) {
            w.u64v(r.cell.coverage);
            writeIdx(r.cell.bulkMaterial);
            writeIdx(r.cell.skinMaterial);
        }
        if (flags & kCellOpening) w.u64v(r.cell.openingCoverage);
    }
    return out;
}

bool LodBlobCodec::decode(const uint8_t* data, size_t size,
                          LodVolume& outVolume, std::vector<std::string>& outPalette) {
    if (!data) return false;
    Reader r{data, size};

    if (r.u32v() != kMagic || !r.ok) return false;
    const uint8_t version = r.u8v();
    if (version != kCodecVersion) return false;      // unknown version: refuse, never guess
    const int level = static_cast<int>(r.u8v());
    const uint8_t flags = r.u8v();
    const bool wide = (flags & kFlagWidePalette) != 0;
    const uint16_t paletteCount = r.u16v();
    const int dx = r.u16v(), dy = r.u16v(), dz = r.u16v();
    if (!r.ok) return false;
    if (dx <= 0 || dy <= 0 || dz <= 0) return false;
    if (level < 0 || level > 31) return false;
    // Cap BEFORE constructing the volume. LodVolume allocates dx*dy*dz cells eagerly, so a row
    // claiming 65535^3 threw std::bad_alloc straight out of this function -- a crash on the read
    // path, from a function whose whole contract is "return false on bad data". The largest
    // legitimate volume is a level-0 chunk at 32^3 = 32768 cells; 64^3 leaves generous headroom
    // while keeping the worst case bounded. Widened to size_t first so the product cannot wrap.
    constexpr size_t kMaxCells = 64u * 64u * 64u;
    const size_t declaredCells = size_t(dx) * size_t(dy) * size_t(dz);
    if (declaredCells > kMaxCells) return false;

    outPalette.clear();
    outPalette.reserve(paletteCount);
    for (uint16_t i = 0; i < paletteCount; ++i) {
        const uint8_t n = r.u8v();
        if (!r.need(n)) return false;
        outPalette.emplace_back(reinterpret_cast<const char*>(r.p), n);
        r.p += n; r.remaining -= n;
    }
    if (!r.ok) return false;

    auto readIdx = [&]() -> uint16_t { return wide ? r.u16v() : uint16_t(r.u8v()); };

    outVolume = LodVolume(glm::ivec3(dx, dy, dz), level);
    const size_t total = size_t(dx) * size_t(dy) * size_t(dz);
    const uint32_t runCount = r.u32v();
    if (!r.ok) return false;

    size_t written = 0;
    for (uint32_t i = 0; i < runCount; ++i) {
        const uint16_t len = r.u16v();
        const uint8_t cf = r.u8v();
        if (!r.ok) return false;
        LodCell cell;
        if (cf & kCellSolid) {
            cell.coverage = r.u64v();
            cell.bulkMaterial = readIdx();
            cell.skinMaterial = readIdx();
            // A cell may not name a palette entry that does not exist. The mesher indexes
            // palette[skinMaterial] directly, so an out-of-range id read off disk is an
            // out-of-bounds read. Refuse the blob rather than clamp it: a wrong material is a
            // silent corruption, and this is data we did not author.
            if (cell.bulkMaterial >= paletteCount || cell.skinMaterial >= paletteCount)
                return false;
        }
        if (cf & kCellOpening) {
            cell.preserveOpening = true;
            cell.openingCoverage = r.u64v();
        }
        if (!r.ok) return false;
        // A run that overruns the volume means the blob disagrees with its own header.
        if (written + len > total) return false;
        for (uint16_t k = 0; k < len; ++k, ++written) {
            const int x = int(written / (size_t(dz) * size_t(dy)));
            const int y = int((written / size_t(dz)) % size_t(dy));
            const int z = int(written % size_t(dz));
            outVolume.at(x, y, z) = cell;
        }
    }
    // Run lengths MUST tile the volume exactly. A short blob would otherwise decode to a
    // partially-populated volume that looks valid and renders holes.
    return written == total;
}

} // namespace Core
} // namespace Phyxel
