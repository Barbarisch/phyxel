#include "core/water/WaterBodyTable.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <map>

namespace Phyxel::Core::Water {

void WaterBodyTable::importGeneration(const std::vector<WaterBodyRecord>& bodies) {
    std::vector<WaterBodyRecord> keep;
    std::map<int, WaterLook> looks;   // G3: a re-import keeps the looks the world set on its bodies
    for (const auto& r : m_records) { if (r.origin != "generation") keep.push_back(r); else if (r.look.any()) looks[r.id] = r.look; }
    for (auto b : bodies) { b.origin = "generation"; if (looks.count(b.id) && !b.look.any()) b.look = looks[b.id]; keep.push_back(b); }
    m_records.swap(keep);
}

const WaterBodyRecord* WaterBodyTable::find(int id) const {
    for (const auto& r : m_records) if (r.id == id) return &r;
    return nullptr;
}

WaterBodyRecord* WaterBodyTable::findMutable(int id) {
    for (auto& r : m_records) if (r.id == id) return &r;
    return nullptr;
}

int WaterBodyTable::bodyAt(int x, int z, const std::function<int(int, int)>& bakeBodyAt, float topY) const {
    const int g = bakeBodyAt ? bakeBodyAt(x, z) : -1;
    if (g >= 0) return g;
    for (const auto& r : m_records) {
        if (r.origin != "av" && r.origin != "region") continue;
        if (x < r.bboxMin.x || x > r.bboxMax.x || z < r.bboxMin.y || z > r.bboxMax.y) continue;
        if (!std::isnan(topY) && std::abs(topY - r.level) > 0.05f) continue;   // another level: another body
        return r.id;
    }
    return kNoBody;
}

int WaterBodyTable::addRegion(WaterBodyRecord r) {
    r.id = m_nextAvId--; r.origin = "region"; r.mass = 0.0; r.displaced = 0.0;
    m_records.push_back(r);
    return r.id;
}

WaterLook WaterBodyTable::lookAt(int x, int z, const std::function<int(int, int)>& bakeBodyAt, float topY) const {
    const int id = bodyAt(x, z, bakeBodyAt, topY);
    if (id == kNoBody) return WaterLook{};
    const WaterBodyRecord* r = find(id);
    return r ? r->look : WaterLook{};
}

WaterBodyRecord* WaterBodyTable::avPondAt(int x, int z) {
    for (auto& r : m_records)
        if (r.origin == "av" && x >= r.bboxMin.x && x <= r.bboxMax.x && z >= r.bboxMin.y && z <= r.bboxMax.y) return &r;
    return nullptr;
}

int WaterBodyTable::credit(const std::vector<ColumnMass>& written, const std::vector<ColumnMass>& seeded,
                           const std::function<int(int, int)>& bakeBodyAt) {
    std::map<std::pair<int, int>, double> seededAt;
    for (const auto& s : seeded) seededAt[{s.x, s.z}] += s.mass;
    std::map<int, int> columnsPerBody;
    // columns that belong to no bake body: one av pond per write-back (or the pond already there)
    WaterBodyRecord* pond = nullptr;
    double pondMass = 0.0; int pondCols = 0; double pondTopSum = 0.0;
    glm::ivec2 pondMin(INT_MAX), pondMax(INT_MIN);
    for (const auto& w : written) {
        const int body = bakeBodyAt ? bakeBodyAt(w.x, w.z) : -1;
        const double delta = w.mass - (seededAt.count({w.x, w.z}) ? seededAt[{w.x, w.z}] : 0.0);
        if (body >= 0) {
            WaterBodyRecord* r = nullptr;
            for (auto& rec : m_records) if (rec.origin == "generation" && rec.id == body) { r = &rec; break; }
            if (!r) { WaterBodyRecord n; n.id = body; n.origin = "generation"; n.cls = "lake"; n.bboxMin = n.bboxMax = glm::ivec2(w.x, w.z); m_records.push_back(n); r = &m_records.back(); }
            r->mass += delta;
            r->bboxMin = glm::min(r->bboxMin, glm::ivec2(w.x, w.z)); r->bboxMax = glm::max(r->bboxMax, glm::ivec2(w.x, w.z));
            ++columnsPerBody[body];
            continue;
        }
        if (!pond) pond = avPondAt(w.x, w.z);
        pondMass += w.mass; ++pondCols; pondTopSum += w.top;
        pondMin = glm::min(pondMin, glm::ivec2(w.x, w.z)); pondMax = glm::max(pondMax, glm::ivec2(w.x, w.z));
    }
    if (pondCols > 0) {
        if (!pond) {
            WaterBodyRecord n; n.id = m_nextAvId--; n.origin = "av"; n.cls = "pond";
            n.bboxMin = pondMin; n.bboxMax = pondMax; n.mass = 0.0;
            m_records.push_back(n); pond = &m_records.back();
            // a new record takes the FULL written mass of its columns: what is there now is the body
            pond->mass = pondMass;
        } else {
            // an existing pond: its columns inside this write-back are REPLACED by what was written
            double seededHere = 0.0;
            for (const auto& w : written) if (bakeBodyAt == nullptr || bakeBodyAt(w.x, w.z) < 0) seededHere += seededAt.count({w.x, w.z}) ? seededAt[{w.x, w.z}] : 0.0;
            pond->mass += pondMass - seededHere;
            pond->bboxMin = glm::min(pond->bboxMin, pondMin); pond->bboxMax = glm::max(pond->bboxMax, pondMax);
        }
        pond->level = static_cast<float>(pondTopSum / pondCols);
        columnsPerBody[pond->id] = pondCols;
    }
    int best = 0, bestCols = 0;
    for (const auto& [id, n] : columnsPerBody)
        if (n > bestCols || (n == bestCols && id > best)) { best = id; bestCols = n; }   // ties: the bake body (id >= 0) over an av pond (< 0)
    return best;
}

void WaterBodyTable::displace(int x, int z, double m3, const std::function<int(int, int)>& bakeBodyAt) {
    const int body = bakeBodyAt ? bakeBodyAt(x, z) : -1;
    WaterBodyRecord* r = nullptr;
    if (body >= 0) { for (auto& rec : m_records) if (rec.origin == "generation" && rec.id == body) { r = &rec; break; } }
    if (!r) r = avPondAt(x, z);
    if (!r) { m_orphanDisplaced += m3; return; }
    r->displaced += m3; r->mass -= m3;
}

double WaterBodyTable::avMass() const {
    double m = 0.0;
    for (const auto& r : m_records) if (r.origin == "av") m += r.mass;
    return m;
}

double WaterBodyTable::displacedTotal() const {
    double d = m_orphanDisplaced;
    for (const auto& r : m_records) d += r.displaced;
    return d;
}

std::string WaterBodyTable::serialize() const {
    nlohmann::json j = nlohmann::json::array();
    for (const auto& r : m_records)
    {
        nlohmann::json row = {{"id", r.id}, {"cls", r.cls}, {"level", r.level}, {"mass", r.mass}, {"displaced", r.displaced},
                              {"bbox", {r.bboxMin.x, r.bboxMin.y, r.bboxMax.x, r.bboxMax.y}}, {"origin", r.origin}};
        if (r.look.any()) row["look"] = nlohmann::json::parse(lookToJson(r.look));   // G3: only when set (old worlds stay byte-identical)
        j.push_back(row);
    }
    return nlohmann::json{{"version", 1}, {"next_av_id", m_nextAvId}, {"orphan_displaced", m_orphanDisplaced}, {"bodies", j}}.dump();
}

bool WaterBodyTable::load(const std::string& text) {
    nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("bodies") || !j["bodies"].is_array()) return false;
    std::vector<WaterBodyRecord> recs;
    for (const auto& b : j["bodies"]) {
        WaterBodyRecord r;
        r.id = b.value("id", 0); r.cls = b.value("cls", std::string("pond")); r.level = b.value("level", 0.0f);
        r.mass = b.value("mass", 0.0); r.displaced = b.value("displaced", 0.0); r.origin = b.value("origin", std::string("av"));
        if (b.contains("look") && b["look"].is_object()) r.look = clampLook(lookFromJson(b["look"].dump()));
        if (b.contains("bbox") && b["bbox"].is_array() && b["bbox"].size() == 4) {
            r.bboxMin = glm::ivec2(b["bbox"][0].get<int>(), b["bbox"][1].get<int>());
            r.bboxMax = glm::ivec2(b["bbox"][2].get<int>(), b["bbox"][3].get<int>());
        }
        recs.push_back(r);
    }
    m_records.swap(recs);
    m_nextAvId = j.value("next_av_id", -1);
    m_orphanDisplaced = j.value("orphan_displaced", 0.0);
    return true;
}

}  // namespace Phyxel::Core::Water
