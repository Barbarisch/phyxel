#include "core/BuildingProgramValidator.h"

#include <algorithm>
#include <deque>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace Phyxel {
namespace Core {

namespace {

bool overlap(const Rect& a, const Rect& b) {
    int ox = std::min(a.x1(), b.x1()) - std::max(a.x, b.x);
    int oz = std::min(a.z1(), b.z1()) - std::max(a.z, b.z);
    return ox > 0 && oz > 0;
}

// Edge-adjacent with a shared span (rooms touch along a wall line).
bool shareWall(const Rect& a, const Rect& b) {
    if (a.x1() == b.x || b.x1() == a.x) {
        int lo = std::max(a.z, b.z), hi = std::min(a.z1(), b.z1());
        if (hi - lo > 0) return true;
    }
    if (a.z1() == b.z || b.z1() == a.z) {
        int lo = std::max(a.x, b.x), hi = std::min(a.x1(), b.x1());
        if (hi - lo > 0) return true;
    }
    return false;
}

bool containsCenter(const Rect& room, const Rect& inner) {
    int cx = inner.x + inner.w / 2;
    int cz = inner.z + inner.d / 2;
    return cx >= room.x && cx < room.x1() && cz >= room.z && cz < room.z1();
}

std::string node(int story, const std::string& roomId) {
    return std::to_string(story) + ":" + roomId;
}

std::string fmt(double v) {
    std::ostringstream os;
    os.setf(std::ios::fixed);
    os.precision(2);
    os << v;
    return os.str();
}

} // namespace

ValidationReport BuildingProgramValidator::validate(const BuildingProgram& program,
                                                    const CharacterScale& scale,
                                                    const RoomProgram* roomProgram) {
    ValidationReport r;

    if (program.stories.empty()) {
        r.addError("no_stories", "program has no stories");
        return r;
    }

    // Effective bounding extent (footprint, or the rooms' max extent if unset).
    int W = program.footprintW, D = program.footprintD;
    for (const auto& s : program.stories)
        for (const auto& rm : s.rooms) { W = std::max(W, rm.rect.x1()); D = std::max(D, rm.rect.z1()); }

    // ---- per-story geometry + scale ----
    for (size_t si = 0; si < program.stories.size(); ++si) {
        const ProgStory& story = program.stories[si];
        const std::string where = "story " + std::to_string(si);

        if (story.height < scale.ceilingMin) {
            r.addError("ceiling_too_low",
                       "interior height " + std::to_string(story.height) + " < min "
                           + std::to_string(scale.ceilingMin),
                       where);
        }

        std::map<std::string, Rect> rooms;
        for (const auto& rm : story.rooms) {
            if (rm.rect.w <= 0 || rm.rect.d <= 0)
                r.addError("degenerate_room", "room '" + rm.id + "' has non-positive size", where);
            if (rm.rect.x < 0 || rm.rect.z < 0 || rm.rect.x1() > W || rm.rect.z1() > D)
                r.addError("room_out_of_bounds", "room '" + rm.id + "' exits the footprint", where);
            if (rooms.count(rm.id))
                r.addError("duplicate_room_id", "room id '" + rm.id + "' is not unique", where);
            rooms[rm.id] = rm.rect;
        }

        // Pairwise overlap.
        for (size_t i = 0; i < story.rooms.size(); ++i)
            for (size_t j = i + 1; j < story.rooms.size(); ++j)
                if (overlap(story.rooms[i].rect, story.rooms[j].rect))
                    r.addError("room_overlap",
                               "rooms '" + story.rooms[i].id + "' and '" + story.rooms[j].id
                                   + "' overlap",
                               where);

        // Portals: perimeter for exterior, adjacency for interior, scale for doors.
        for (const auto& p : story.portals) {
            bool ext = (p.a == "exterior" || p.b == "exterior");
            if (ext) {
                bool onPerim = (p.px == 0 || p.px == W || p.pz == 0 || p.pz == D);
                if (!onPerim)
                    r.addError("exterior_portal_off_perimeter",
                               "exterior opening at (" + std::to_string(p.px) + ","
                                   + std::to_string(p.pz) + ") is not on the building perimeter",
                               where);
            } else {
                auto ia = rooms.find(p.a), ib = rooms.find(p.b);
                if (ia == rooms.end() || ib == rooms.end())
                    r.addError("portal_unknown_room",
                               "interior portal references a room not on this story", where);
                else if (!shareWall(ia->second, ib->second))
                    r.addError("portal_rooms_not_adjacent",
                               "rooms '" + p.a + "' and '" + p.b + "' do not share a wall", where);
            }
            if (p.kind == "door" || p.kind == "arch") {
                if (p.height < scale.doorClearMin)
                    r.addError("door_too_short",
                               "opening height " + std::to_string(p.height) + " < min "
                                   + std::to_string(scale.doorClearMin),
                               where);
                if (p.width < scale.doorWidthMin)
                    r.addError("door_too_narrow",
                               "opening width " + std::to_string(p.width) + " < min "
                                   + std::to_string(scale.doorWidthMin),
                               where);
            }
        }
    }

    // ---- function: entrance + full reachability across stories ----
    std::map<std::string, std::vector<std::string>> adj;
    std::set<std::string> roomNodes;
    auto addEdge = [&](const std::string& a, const std::string& b) {
        adj[a].push_back(b);
        adj[b].push_back(a);
    };

    for (size_t si = 0; si < program.stories.size(); ++si)
        for (const auto& rm : program.stories[si].rooms)
            roomNodes.insert(node((int)si, rm.id));

    bool hasEntrance = false;
    for (size_t si = 0; si < program.stories.size(); ++si) {
        for (const auto& p : program.stories[si].portals) {
            if (!p.passable()) continue;             // windows don't connect
            auto nodeOf = [&](const std::string& ep) {
                return ep == "exterior" ? std::string("exterior") : node((int)si, ep);
            };
            if (p.a == "exterior" || p.b == "exterior") hasEntrance = true;
            addEdge(nodeOf(p.a), nodeOf(p.b));
        }
    }

    // Stairs link a room on fromStory to a room on toStory (the one containing the well).
    for (size_t si = 0; si < program.stories.size(); ++si) {
        for (const auto& st : program.stories[si].stairs) {
            if (st.fromStory < 0 || st.toStory < 0 ||
                st.fromStory >= (int)program.stories.size() ||
                st.toStory >= (int)program.stories.size())
                continue;
            std::string from, to;
            for (const auto& rm : program.stories[st.fromStory].rooms)
                if (containsCenter(rm.rect, st.rect)) { from = node(st.fromStory, rm.id); break; }
            for (const auto& rm : program.stories[st.toStory].rooms)
                if (containsCenter(rm.rect, st.rect)) { to = node(st.toStory, rm.id); break; }
            if (!from.empty() && !to.empty()) addEdge(from, to);
            else r.addWarning("stair_room_unresolved",
                              "a stair does not sit inside a room on both stories");
        }
    }

    if (!hasEntrance)
        r.addError("no_entrance", "no exterior door/arch — the building cannot be entered");

    // BFS from exterior.
    std::set<std::string> visited;
    std::deque<std::string> q;
    q.push_back("exterior");
    visited.insert("exterior");
    while (!q.empty()) {
        std::string n = q.front(); q.pop_front();
        for (const auto& m : adj[n])
            if (visited.insert(m).second) q.push_back(m);
    }
    for (const auto& rn : roomNodes)
        if (!visited.count(rn))
            r.addError("room_unreachable",
                       "room '" + rn + "' cannot be reached from the entrance");

    // ---- room-program typology gate (grounded, period sizing from room_program.json) ----
    if (roomProgram) {
        const RoomProgram& rp = *roomProgram;
        const int shortSide = std::min(W, D), longSide = std::max(W, D);

        if (rp.widthMax > 0.0 && shortSide > rp.widthMax + 1e-6)
            r.addError("footprint_too_wide",
                       "building width " + std::to_string(shortSide) + " exceeds the '" + rp.name
                           + "' typology max " + fmt(rp.widthMax) + " (e.g. cruck span limit)",
                       "footprint");
        if (rp.widthMin > 0.0 && shortSide + 1e-6 < rp.widthMin)
            r.addWarning("footprint_narrow",
                         "building width " + std::to_string(shortSide) + " is below the '" + rp.name
                             + "' typology min " + fmt(rp.widthMin),
                         "footprint");

        if (rp.proportionMax > 0.0 && shortSide > 0) {
            const double ratio = static_cast<double>(longSide) / static_cast<double>(shortSide);
            if (ratio > rp.proportionMax + 1e-6)
                r.addError("footprint_too_long",
                           "length:width " + fmt(ratio) + " exceeds the '" + rp.name + "' max "
                               + fmt(rp.proportionMax),
                           "footprint");
            const double pmin = rp.proportionMin > 0.0 ? rp.proportionMin : 1.0;
            if (ratio + 1e-6 < pmin)
                r.addError("footprint_too_square",
                           "length:width " + fmt(ratio) + " is below the '" + rp.name + "' min "
                               + fmt(pmin),
                           "footprint");
        }

        // Usable minimum room dimension (anthropometric — a person must occupy + move).
        for (size_t si = 0; si < program.stories.size(); ++si)
            for (const auto& rm : program.stories[si].rooms) {
                const int rmin = std::min(rm.rect.w, rm.rect.d);
                if (rmin + 1e-6 < scale.minRoomDim)
                    r.addError("room_too_narrow",
                               "room '" + rm.id + "' short side " + std::to_string(rmin)
                                   + " < usable min " + fmt(scale.minRoomDim),
                               "story " + std::to_string(si));
            }
    }

    return r;
}

} // namespace Core
} // namespace Phyxel
