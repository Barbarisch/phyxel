#include "core/voxfound/EmissiveLights.h"

#include <algorithm>
#include <cmath>

#include "core/MaterialRegistry.h"

namespace Phyxel::Core::VoxFound {

std::vector<EmitterMaterial> emitterMaterials(const std::vector<std::string>& palette) {
    std::vector<EmitterMaterial> em(std::max<size_t>(palette.size(), 1));
    auto& reg = MaterialRegistry::instance();
    for (size_t i = 1; i < palette.size(); ++i) {
        const auto* md = palette[i].empty() ? nullptr : reg.getMaterial(palette[i]);
        if (!md || (!md->emissive && md->emissiveStrength <= 0.0f)) continue;
        const float scale15 = md->emissive ? 15.0f : glm::clamp(md->emissiveStrength * 4.0f, 2.0f, 10.0f);
        em[i].t = scale15 / 15.0f;
        em[i].hue = glm::max(md->physics.colorTint, glm::vec3(0.0f));
    }
    return em;
}

bool anyEmitter(const std::vector<EmitterMaterial>& em) {
    for (const auto& e : em)
        if (e.t > 0.0f) return true;
    return false;
}

namespace {
struct Accum {
    glm::vec3 weightedPos{0.0f}, weightedHue{0.0f}, bmin{1e30f}, bmax{-1e30f};
    float power = 0.0f, volume = 0.0f, maxT = 0.0f;
    void add(const glm::vec3& centre, const EmitterMaterial& e, float volumeCubes, float half) {
        const float p = e.t * volumeCubes;
        weightedPos += centre * p;
        weightedHue += e.hue * p;
        power += p;
        volume += volumeCubes;
        maxT = std::max(maxT, e.t);
        bmin = glm::min(bmin, centre - glm::vec3(half));
        bmax = glm::max(bmax, centre + glm::vec3(half));
    }
    bool light(EmissiveLight& l) const {
        if (power <= 0.0f || maxT <= 0.0f) return false;
        l.worldPos = weightedPos / power;
        l.color = glm::max(weightedHue / power, glm::vec3(0.0f));
        l.intensity = maxT;
        const float sizeScale =
            glm::clamp(1.0f + 0.25f * std::log(std::max(volume, 1e-6f)) / std::log(27.0f), 0.5f, 1.0f);
        l.radius = std::max(2.0f, 15.0f * maxT * sizeScale);
        const glm::vec3 far = glm::max(l.worldPos - bmin, bmax - l.worldPos);
        l.extent = glm::length(glm::max(far, glm::vec3(0.0f)));
        return true;
    }
};
}  // namespace

void emissiveLightsOfChunk(const BlockStore& store, glm::ivec3 chunk, const std::vector<EmitterMaterial>& em,
                           std::vector<EmissiveLight>& out) {
    const glm::ivec3 nb = store.dim() / 8;
    const glm::vec3 originM = glm::vec3(chunk * (store.dim() / 8));   // world metres of block (0,0,0)
    bool table[256] = {};
    for (size_t m = 1; m < em.size() && m < 256; ++m) table[m] = em[m].t > 0.0f;
    auto emits = [&](uint8_t m) { return table[m]; };
    for (int bx = 0; bx < nb.x; ++bx)
        for (int by = 0; by < nb.y; ++by)
            for (int bz = 0; bz < nb.z; ++bz) {
                const glm::ivec3 b(bx, by, bz);
                uint8_t m = 0;
                Accum a;
                if (store.uniformBlock(b, m)) {
                    if (!emits(m)) continue;
                    a.add(originM + glm::vec3(b) + glm::vec3(0.5f), em[m], 1.0f, 0.5f);   // a whole glow cube
                } else {
                    const uint8_t* mats = store.detailedMaterials(b);
                    bool any = false;
                    for (int i = 0; i < 512 && !any; ++i) any = table[mats[i]];
                    if (!any) continue;   // the common detailed block: no emitter, one tight pass
                    for (int i = 0; i < 512; ++i) {
                        const glm::ivec3 v(i >> 6, (i >> 3) & 7, i & 7);
                        const uint8_t vm = mats[(v.z & 7) + 8 * ((v.y & 7) + 8 * (v.x & 7))];
                        if (!emits(vm)) continue;
                        a.add(originM + glm::vec3(b) + (glm::vec3(v) + glm::vec3(0.5f)) / 8.0f, em[vm], 1.0f / 512.0f,
                              1.0f / 16.0f);
                    }
                }
                EmissiveLight l;
                if (a.light(l)) out.push_back(l);
            }
}

std::vector<uint8_t> mirrorMaterials(const std::vector<std::string>& palette) {
    std::vector<uint8_t> t(256, 0);
    auto& reg = MaterialRegistry::instance();
    for (size_t i = 1; i < palette.size() && i < 256; ++i) {
        const auto* md = palette[i].empty() ? nullptr : reg.getMaterial(palette[i]);
        t[i] = (md && md->isMirror) ? 1 : 0;
    }
    return t;
}

bool anyMirror(const std::vector<uint8_t>& table) {
    for (uint8_t v : table)
        if (v) return true;
    return false;
}

void mirrorFacesOfChunk(const BlockStore& store, glm::ivec3 chunk, const std::vector<uint8_t>& table, MirrorFaces& acc,
                        const std::function<uint8_t(glm::ivec3)>* outside) {
    if (table.size() < 256) return;
    const glm::ivec3 dim = store.dim(), nb = dim / 8;
    const glm::ivec3 base = chunk * dim;   // world voxel of the store's voxel 0
    auto matAt = [&](glm::ivec3 p) -> uint8_t {   // store voxel p (may lie outside the store)
        if (store.in(p)) return store.at(p);
        return outside ? (*outside)(base + p) : uint8_t(0);
    };
    for (int bx = 0; bx < nb.x; ++bx)
        for (int by = 0; by < nb.y; ++by)
            for (int bz = 0; bz < nb.z; ++bz) {
                const glm::ivec3 b(bx, by, bz);
                uint8_t m = 0;
                if (store.uniformBlock(b, m)) {
                    if (!table[m]) continue;
                } else {
                    const uint8_t* mats = store.detailedMaterials(b);
                    bool any = false;
                    for (int i = 0; i < 512 && !any; ++i) any = table[mats[i]] != 0;
                    if (!any) continue;
                }
                for (int i = 0; i < 512; ++i) {
                    const glm::ivec3 p = b * 8 + glm::ivec3(i >> 6, (i >> 3) & 7, i & 7);
                    if (!table[store.at(p)]) continue;
                    for (int axis = 0; axis < 3; ++axis)
                        for (int dir : {-1, 1}) {
                            glm::ivec3 q = p;
                            q[axis] += dir;
                            if (matAt(q) != 0) continue;   // hidden: any solid neighbour (the mesher's rule)
                            const int k = 2 * axis + (dir > 0);
                            glm::dvec3 c = (glm::dvec3(base + p) + 0.5) / 8.0;
                            c[axis] += 0.5 * dir / 8.0;   // the face's centre
                            ++acc.count[k];
                            acc.sum[k] += c;
                        }
                }
            }
}

bool mirrorPlane(const MirrorFaces& acc, glm::vec3& point, glm::vec3& normal) {
    int best = -1;
    for (int k = 0; k < 6; ++k)
        if (acc.count[k] > 0 && (best < 0 || acc.count[k] > acc.count[best])) best = k;
    if (best < 0) return false;
    normal = glm::vec3(0.0f);
    normal[best / 2] = (best & 1) ? 1.0f : -1.0f;
    point = glm::vec3(acc.sum[best] / double(acc.count[best]));
    return true;
}

}  // namespace Phyxel::Core::VoxFound
