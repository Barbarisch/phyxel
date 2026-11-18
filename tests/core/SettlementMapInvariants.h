// The settlement MAP's L2 invariants, counted on a planned map (moved unchanged out of SettlementMapTest.cpp,
// pm_chain380.txt, so SettlementMapLiveSiteTest measures a map planned on a baked world with the same counter).
#pragma once

#include <algorithm>
#include <climits>
#include <cmath>
#include <functional>

#include <glm/glm.hpp>

#include "core/SettlementMap.h"
#include "core/SettlementMapPlanner.h"

namespace MapInvariants {
using namespace Phyxel;
using namespace Phyxel::Core;

// the invariants, counted (each test asserts the ones it is about and prints the rest)
struct Violations { int lotNoWay = 0, lotDoorOffWay = 0, overlaps = 0, wayThroughFootprint = 0, lotOutside = 0, lotRelief = 0, badWay = 0, wayInWater = 0; };
inline Violations measure(const SettlementMap& m, const std::function<int(int, int)>& heightAt, int reliefCap, const std::function<bool(int, int)>& waterAt = {}) {
    Violations v;
    for (const auto& l : m.lots) {
        if (l.way < 0 || l.way >= static_cast<int>(m.ways.size())) { ++v.lotNoWay; }
        else {
            // the door-approach cell within 1.5 u of the way's POLYLINE (point to segment - chain 63 measured
            // node distance, which read every street-tier lot as off its two-node spine)
            const MapWay& w = m.ways[static_cast<size_t>(l.way)];
            const glm::ivec2 d = lotDoorApproach(l);
            float best = 1e9f;
            for (size_t k = 0; k + 1 < w.nodes.size(); ++k) {
                const MapNode* a = m.node(w.nodes[k]); const MapNode* b = m.node(w.nodes[k + 1]);
                if (!a || !b) continue;
                const glm::vec2 pa(static_cast<float>(a->x), static_cast<float>(a->z)), pb(static_cast<float>(b->x), static_cast<float>(b->z)), pd(static_cast<float>(d.x), static_cast<float>(d.y));
                const glm::vec2 ab = pb - pa;
                const float t = glm::dot(ab, ab) > 0.0f ? std::clamp(glm::dot(pd - pa, ab) / glm::dot(ab, ab), 0.0f, 1.0f) : 0.0f;
                best = std::min(best, glm::length(pd - (pa + ab * t)));
            }
            // the growth's way STARTS at the door-approach cell (exact: audit E2 - the polyline tolerance is for a
            // street-tier spine, whose rows abut the street's edge while the polyline is its centre line)
            const MapNode* first = w.nodes.empty() ? nullptr : m.node(w.nodes.front());
            const bool startsAtDoor = first && first->x == d.x && first->z == d.y;
            // chain 91: a croft whose door cell lies ON an earlier path fronts that path (no one-node way of its own)
            if (m.morphology == "cluster") { if (!startsAtDoor && best > 0.5f) ++v.lotDoorOffWay; }
            else if (!startsAtDoor && best > 1.5f + 0.5f * w.width) ++v.lotDoorOffWay;
        }
        if (l.rect.x < 0 || l.rect.z < 0 || l.rect.x1() > m.W || l.rect.z1() > m.D) ++v.lotOutside;
        int lo = INT_MAX, hi = INT_MIN;
        for (int z = std::max(0, l.rect.z); z < std::min(m.D, l.rect.z1()); ++z)
            for (int x = std::max(0, l.rect.x); x < std::min(m.W, l.rect.x1()); ++x) { const int y = heightAt(x, z); lo = std::min(lo, y); hi = std::max(hi, y); }
        if (lo <= hi && hi - lo > reliefCap) ++v.lotRelief;
    }
    for (size_t i = 0; i < m.lots.size(); ++i)
        for (size_t j = i + 1; j < m.lots.size(); ++j) {
            const Rect& a = m.lots[i].rect; const Rect& b = m.lots[j].rect;
            if (a.x < b.x1() && b.x < a.x1() && a.z < b.z1() && b.z < a.z1()) ++v.overlaps;
        }
    for (const auto& w : m.ways) {
        if (w.nodes.size() < 2 || w.width <= 0.0f) { ++v.badWay; continue; }
        for (size_t k = 0; k + 1 < w.nodes.size(); ++k) {
            const MapNode* a = m.node(w.nodes[k]); const MapNode* b = m.node(w.nodes[k + 1]);
            if (!a || !b) { ++v.badWay; continue; }
            const float len = std::hypot(static_cast<float>(b->x - a->x), static_cast<float>(b->z - a->z));
            const int steps = std::max(1, static_cast<int>(std::ceil(len * 2.0f)));
            bool through = false;
            for (int s = 0; s <= steps && !through; ++s) {
                const float t = static_cast<float>(s) / static_cast<float>(steps);
                const float x = static_cast<float>(a->x) + t * static_cast<float>(b->x - a->x), z = static_cast<float>(a->z) + t * static_cast<float>(b->z - a->z);
                for (const auto& l : m.lots) {
                    const Rect& f = l.footprint;
                    if (x >= static_cast<float>(f.x) && x < static_cast<float>(f.x1()) && z >= static_cast<float>(f.z) && z < static_cast<float>(f.z1())) { through = true; break; }
                }
            }
            if (through) ++v.wayThroughFootprint;
            if (waterAt) {   // a way never crosses water (M0): every cell of the segment, dry
                const int cells = std::max(1, std::max(std::abs(b->x - a->x), std::abs(b->z - a->z)));
                for (int c = 0; c <= cells; ++c) {
                    const int x = a->x + (b->x - a->x) * c / cells, z = a->z + (b->z - a->z) * c / cells;
                    if (waterAt(x, z)) { ++v.wayInWater; break; }
                }
            }
        }
    }
    return v;
}

}  // namespace MapInvariants
