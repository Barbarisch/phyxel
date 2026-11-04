// docs/MaterialSimPlan.md §6a/§6c — MatSim P1b lab service. See the header.
#include "core/matsim/MatSimLab.h"
#include "core/matsim/MatSimGpu.h"
#include "core/MaterialRegistry.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace Phyxel::Core::MatSim {

using Json = MatSimLab::Json;

namespace {
constexpr double kTick = 1.0 / 60.0;
constexpr int kMaxTicksPerFrame = 4;
constexpr int kMaxStep = 100000;

std::string cellText(Cell c) {
    return std::string(materialTable()[cellMaterial(c)].name) + " " + std::to_string(cellFill(c));
}
} // namespace

MatSimLab::MatSimLab(MicroSolidFn occupancy) : occ_(std::move(occupancy)) {}
MatSimLab::~MatSimLab() = default;

const std::vector<std::string>& MatSimLab::actions() {
    static const std::vector<std::string> a = {"create", "fill_box", "step",  "realtime", "set_cell_size", "resolidify",
                                               "ledger", "stats",    "view",  "clear",    "parity", "slices"};
    return a;
}

bool MatSimLab::initGpu(VkDevice device, VkPhysicalDevice physical, VkQueue queue, uint32_t family,
                        const std::string& shaderDir, std::string* err) {
    gpu_ = std::make_unique<MatSimGpu>();
    if (!gpu_->init(device, physical, queue, family, shaderDir, err)) {
        gpuError_ = err ? *err : "init failed";
        gpu_.reset();
        return false;
    }
    return true;
}

bool MatSimLab::ensureHost() {
    if (hostCurrent_) return true;
    std::string err;
    if (gpu_ && gpu_->download(core_, &err)) {
        hostCurrent_ = true;
        return true;
    }
    gpuError_ = "download: " + err;   // the host copy stays stale and every caller refuses: no lost ticks
    return false;
}

bool MatSimLab::ensureGpu() {
    if (!useGpu_ || !gpu_) return false;
    if (gpuCurrent_) return true;
    ensureHost();
    std::string err;
    if (!gpu_->upload(core_, &err)) {
        gpuError_ = "upload: " + err;
        useGpu_ = false;   // fall back to the CPU, loudly (stats.backend, stats.gpu_error)
        return false;
    }
    gpuCurrent_ = true;
    return true;
}

StepStats MatSimLab::doStep(int n) {
    StepStats st;
    if (!core_.created() || n <= 0) return st;
    const auto t0 = std::chrono::steady_clock::now();
    if (ensureGpu()) {
        st = gpu_->step(n);
        hostCurrent_ = false;
        lastGpuMsPerTick_ = gpu_->lastGpuMsPerTick();
    } else {
        if (!ensureHost()) return st;
        st = core_.step(n);
        gpuCurrent_ = false;
        lastGpuMsPerTick_ = -1.0;
    }
    lastWallMsPerTick_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / n;
    lastEligible_ = st.lastTickEligible;
    if (st.eligibleBlocks > 0) facesDirty_ = true;
    return st;
}

void MatSimLab::update(double dt) {
    if (!core_.created() || !realtime_) return;
    acc_ += std::max(0.0, dt);
    int ticks = static_cast<int>(acc_ / kTick);
    acc_ -= ticks * kTick;
    if (ticks > kMaxTicksPerFrame) {
        droppedTicks_ += uint64_t(ticks - kMaxTicksPerFrame);
        ticks = kMaxTicksPerFrame;
    }
    if (ticks > 0) doStep(ticks);
    if (view_ && facesDirty_ && ensureHost()) rebuildFaces();
}

Json MatSimLab::ledgerJson() const {
    Json units = Json::object(), expected = Json::object();
    const Ledger l = core_.ledger();
    bool exact = true;
    for (int m = 1; m < 255; ++m) {
        if (l[m]) units[materialTable()[m].name] = l[m];
        if (core_.expectedLedger()[m]) expected[materialTable()[m].name] = core_.expectedLedger()[m];
        if (l[m] != core_.expectedLedger()[m]) exact = false;
    }
    return {{"units", units}, {"expected", expected}, {"exact", exact}, {"full_units", core_.full()}};
}

Json MatSimLab::boxJson() const {
    if (!core_.created()) return nullptr;
    const double h = 1.0 / core_.cellsPerM();
    const IVec3 o = core_.origin(), s = core_.size();
    return {{"min", {{"x", o.x * h}, {"y", o.y * h}, {"z", o.z * h}}},
            {"size", {{"x", s.x * h}, {"y", s.y * h}, {"z", s.z * h}}},
            {"cells", {s.x, s.y, s.z}},
            {"cell_count", uint64_t(s.x) * uint64_t(s.y) * uint64_t(s.z)}};
}

void MatSimLab::echo(Json& out) const {
    out["ledger"] = core_.created() ? ledgerJson() : Json(nullptr);
    out["cells_per_m"] = core_.cellsPerM();
    out["box"] = boxJson();
}

bool MatSimLab::metresToCells(const Json& v, IVec3& out, std::string& why, const char* what) const {
    if (!v.is_object() || !v.contains("x") || !v.contains("y") || !v.contains("z") || !v["x"].is_number() ||
        !v["y"].is_number() || !v["z"].is_number()) {
        why = std::string(what) + " must be {x,y,z} in metres";
        return false;
    }
    const int cpm = core_.cellsPerM();
    const double m[3] = {v["x"].get<double>(), v["y"].get<double>(), v["z"].get<double>()};
    int c[3];
    for (int i = 0; i < 3; ++i) {
        const double s = m[i] * cpm, r = std::round(s);
        if (!std::isfinite(s) || std::fabs(s - r) > 1e-6 || std::fabs(r) > 1e8) {
            why = std::string(what) + " must lie on the " + std::to_string(cpm) + " cells/m lattice";
            return false;
        }
        c[i] = int(r);
    }
    out = {c[0], c[1], c[2]};
    return true;
}

void MatSimLab::handle(const std::string& action, const Json& p, Json& out) {
    out = Json::object();
    auto refuse = [&](const std::string& why) { out["error"] = why; echo(out); };
    if (action != "create" && action != "stats" && action != "clear" && action != "view" && action != "realtime" &&
        !core_.created())
        return refuse("no lab box: POST /api/matsim/create first");

    if (action == "create") {
        if (!p.contains("min") || !p.contains("size") || !p.contains("cells_per_m") || !p["cells_per_m"].is_number_integer())
            return refuse("create needs min{x,y,z}, size{x,y,z} (metres) and cells_per_m (1, 3 or 9)");
        const int cpm = p["cells_per_m"].get<int>();
        auto num = [&](const char* k, const char* a) -> double {
            const Json& v = p[k];
            return (v.is_object() && v.contains(a) && v[a].is_number()) ? v[a].get<double>() : std::nan("");
        };
        Config cfg;
        cfg.seed = p.value("seed", 1u);
        cfg.sleep = p.value("sleep", true);
        const std::string backend = p.value("backend", std::string(gpu_ ? "gpu" : "cpu"));
        if (backend != "gpu" && backend != "cpu") return refuse("backend must be \"gpu\" or \"cpu\"");
        if (backend == "gpu" && !gpu_) return refuse("backend gpu unavailable: " + (gpuError_.empty() ? std::string("no device") : gpuError_));
        MatSimCore fresh;
        std::string why;
        if (!fresh.createMetres(num("min", "x"), num("min", "y"), num("min", "z"), num("size", "x"), num("size", "y"),
                                num("size", "z"), cpm, occ_, &why, cfg))
            return refuse(why);   // no partial create: the old box (if any) is untouched
        if (gpu_) gpu_->release();
        core_ = std::move(fresh);
        useGpu_ = backend == "gpu";
        hostCurrent_ = true;
        gpuCurrent_ = false;
        realtime_ = false;
        acc_ = 0.0;
        droppedTicks_ = 0;
        lastEligible_ = 0;
        facesDirty_ = true;
        gpuError_.clear();
        out["created"] = true;
        out["backend"] = useGpu_ ? "gpu" : "cpu";
        out["seed"] = cfg.seed;
        out["sleep"] = cfg.sleep;
    } else if (action == "fill_box") {
        uint8_t mat = 0;
        const std::string name = p.value("material", std::string());
        if (!materialId(name, mat) || (mat != kEmpty && materialTable()[mat].phase != Phase::Liquid &&
                                       materialTable()[mat].phase != Phase::Powder))
            return refuse("material must be one of Empty, Water, Sand, Snow, Gravel (got \"" + name + "\")");
        IVec3 lo, sz;
        std::string why;
        if (!metresToCells(p.value("min", Json()), lo, why, "min") || !metresToCells(p.value("size", Json()), sz, why, "size"))
            return refuse(why);
        if (sz.x <= 0 || sz.y <= 0 || sz.z <= 0) return refuse("size must be positive on every axis");
        if (!p.contains("fraction") || !p["fraction"].is_number()) return refuse("fraction (0-1) is required");
        const double f = p["fraction"].get<double>();
        if (!(f >= 0.0 && f <= 1.0)) return refuse("fraction must be within 0-1");
        if (!ensureHost()) return refuse("the device state could not be read back: " + gpuError_);
        const FillResult r = core_.fillBox(mat, lo, sz, f);
        gpuCurrent_ = false;
        facesDirty_ = true;
        out["cells_written"] = r.cellsWritten;
        out["units_written"] = r.unitsWritten;
        out["solid_skipped"] = r.solidSkipped;
    } else if (action == "step") {
        if (!p.contains("n") || !p["n"].is_number_integer()) return refuse("n (0-100000) is required");
        const long long n = p["n"].get<long long>();
        if (n < 0 || n > kMaxStep) return refuse("n must be within 0-100000");
        const StepStats st = doStep(int(n));
        out["ticks"] = st.ticks;
        out["eligible_blocks"] = st.eligibleBlocks;
        out["evaluated_blocks"] = st.evaluatedBlocks;
        out["last_tick_eligible"] = st.lastTickEligible;
        out["gpu_ms_per_tick"] = lastGpuMsPerTick_;
        out["wall_ms_per_tick"] = lastWallMsPerTick_;
        if (!ensureHost()) return refuse("the device state could not be read back: " + gpuError_);
    } else if (action == "realtime") {
        if (!p.contains("on") || !p["on"].is_boolean()) return refuse("on (bool) is required");
        realtime_ = p["on"].get<bool>();
        acc_ = 0.0;
        out["realtime"] = realtime_;
    } else if (action == "set_cell_size") {
        if (!p.contains("cells_per_m") || !p["cells_per_m"].is_number_integer()) return refuse("cells_per_m (1, 3 or 9) is required");
        if (!ensureHost()) return refuse("the device state could not be read back: " + gpuError_);
        const Json before = ledgerJson();
        std::string why;
        if (!core_.setCellSize(p["cells_per_m"].get<int>(), occ_, &why)) {
            out["refused"] = true;
            out["ledger_before"] = before;
            return refuse(why);
        }
        gpuCurrent_ = false;
        facesDirty_ = true;
        const Json after = ledgerJson();
        out["ledger_before"] = before;
        out["ledger_after"] = after;
        out["ledger_match"] = before["units"] == after["units"];
    } else if (action == "resolidify") {
        if (!ensureHost()) return refuse("the device state could not be read back: " + gpuError_);
        core_.resolidify(occ_);
        gpuCurrent_ = false;
        facesDirty_ = true;
        out["unknown_cells"] = core_.unknownCells();
        out["trapped_units"] = core_.trappedUnits();
    } else if (action == "ledger") {
        if (!ensureHost()) return refuse("the device state could not be read back: " + gpuError_);
    } else if (action == "stats") {
        if (core_.created()) ensureHost();
        out["created"] = core_.created();
        out["backend"] = core_.created() ? (useGpu_ ? "gpu" : "cpu") : (gpu_ ? "gpu available" : "cpu only");
        out["tick"] = core_.tick();
        out["sim_seconds"] = double(core_.tick()) * kTick;
        out["last_tick_eligible"] = lastEligible_;
        out["unknown_cells"] = core_.unknownCells();
        out["trapped_units"] = core_.trappedUnits();
        out["gpu_ms_per_tick"] = lastGpuMsPerTick_;
        out["wall_ms_per_tick"] = lastWallMsPerTick_;
        out["realtime"] = realtime_;
        out["realtime_ticks_dropped"] = droppedTicks_;
        out["view"] = view_;
        out["view_faces"] = faces_.size();
        out["view_faces_cut"] = facesCut_;
        out["seed"] = core_.config().seed;
        out["sleep"] = core_.config().sleep;
        if (!gpuError_.empty()) out["gpu_error"] = gpuError_;
    } else if (action == "view") {
        if (!p.contains("on") || !p["on"].is_boolean()) return refuse("on (bool) is required");
        view_ = p["on"].get<bool>();
        if (view_ && core_.created()) { if (!ensureHost()) return refuse("the device state could not be read back: " + gpuError_); rebuildFaces(); }
        if (!view_) faces_.clear();
        out["view"] = view_;
        out["view_faces"] = faces_.size();
        out["view_faces_cut"] = facesCut_;
    } else if (action == "clear") {
        core_.clear();
        if (gpu_) gpu_->release();
        hostCurrent_ = true;
        gpuCurrent_ = false;
        realtime_ = false;
        faces_.clear();
        out["cleared"] = true;
    } else if (action == "slices") {
        // Read-only: matter per slice along one axis (m^3 of fill, per material), optionally inside a
        // sub-box (metres, on the lattice). What the dam-break front and U-tube levels are read from.
        const std::string axis = p.value("axis", std::string("x"));
        if (axis != "x" && axis != "y" && axis != "z") return refuse("axis must be x, y or z");
        IVec3 lo = core_.origin(), hi{core_.origin().x + core_.size().x, core_.origin().y + core_.size().y,
                                     core_.origin().z + core_.size().z};
        std::string why;
        if (p.contains("min") || p.contains("size")) {
            IVec3 sz;
            if (!metresToCells(p.value("min", Json()), lo, why, "min") || !metresToCells(p.value("size", Json()), sz, why, "size"))
                return refuse(why);
            hi = {lo.x + sz.x, lo.y + sz.y, lo.z + sz.z};
        }
        if (!ensureHost()) return refuse("the device state could not be read back: " + gpuError_);
        const int ax = axis == "x" ? 0 : axis == "y" ? 1 : 2;
        const int a0 = ax == 0 ? lo.x : ax == 1 ? lo.y : lo.z, a1 = ax == 0 ? hi.x : ax == 1 ? hi.y : hi.z;
        if (a1 <= a0 || a1 - a0 > 4096) return refuse("the sub-box is empty or longer than 4096 cells on the axis");
        const double h = 1.0 / core_.cellsPerM(), cellM3 = h * h * h / double(core_.full());
        std::vector<std::vector<double>> per(5, std::vector<double>(size_t(a1 - a0), 0.0));
        for (int y = std::max(lo.y, core_.origin().y); y < std::min(hi.y, core_.origin().y + core_.size().y); ++y)
            for (int z = std::max(lo.z, core_.origin().z); z < std::min(hi.z, core_.origin().z + core_.size().z); ++z)
                for (int x = std::max(lo.x, core_.origin().x); x < std::min(hi.x, core_.origin().x + core_.size().x); ++x) {
                    const Cell c = core_.at({x, y, z});
                    const uint8_t m = cellMaterial(c);
                    if (m == kEmpty || m == kSolid || m > 4) continue;
                    const int k = (ax == 0 ? x : ax == 1 ? y : z) - a0;
                    per[m][size_t(k)] += cellFill(c) * cellM3;
                }
        Json sl = Json::object();
        for (int m = 1; m <= 4; ++m) {
            double sum = 0;
            for (double v : per[m]) sum += v;
            if (sum > 0) sl[materialTable()[m].name] = per[m];
        }
        out["axis"] = axis;
        out["first_slice_m"] = a0 * h;
        out["slice_m"] = h;
        out["m3"] = sl;
    } else if (action == "parity") {
        // Live L4 check: the same ticks on the CPU reference and on the device, compared cell by cell.
        if (!useGpu_ || !gpu_) return refuse("parity needs the gpu backend");
        if (!p.contains("n") || !p["n"].is_number_integer()) return refuse("n (1-5000) is required");
        const int n = p["n"].get<int>();
        if (n < 1 || n > 5000) return refuse("n must be within 1-5000");
        if (!ensureHost()) return refuse("the device state could not be read back: " + gpuError_);
        if (!ensureGpu()) return refuse("parity: the device copy could not be uploaded: " + gpuError_);
        MatSimCore ref = core_;
        const StepStats sc = ref.step(n);
        const StepStats sg = doStep(n);
        if (!useGpu_ || hostCurrent_) return refuse("parity: the step did not run on the device: " + gpuError_);
        if (!ensureHost()) return refuse("the device state could not be read back: " + gpuError_);
        Json first = nullptr;
        uint64_t differing = 0;
        const IVec3 o = core_.origin(), s = core_.size();
        for (int y = 0; y < s.y; ++y)
            for (int z = 0; z < s.z; ++z)
                for (int x = 0; x < s.x; ++x) {
                    const IVec3 w{o.x + x, o.y + y, o.z + z};
                    if (ref.at(w) == core_.at(w)) continue;
                    if (!differing) first = {{"cell", {w.x, w.y, w.z}}, {"cpu", cellText(ref.at(w))}, {"gpu", cellText(core_.at(w))}};
                    ++differing;
                }
        const bool marks = ref.marksTick1() == core_.marksTick1() && ref.marksTick2() == core_.marksTick2();
        out["ticks"] = n;
        out["identical"] = differing == 0 && marks && sc.eligibleBlocks == sg.eligibleBlocks &&
                           sc.evaluatedBlocks == sg.evaluatedBlocks && ref.tick() == core_.tick();
        out["differing_cells"] = differing;
        out["first_difference"] = first;
        out["marks_identical"] = marks;
        out["eligible_cpu_gpu"] = {sc.eligibleBlocks, sg.eligibleBlocks};
        out["evaluated_cpu_gpu"] = {sc.evaluatedBlocks, sg.evaluatedBlocks};
    } else {
        out["error"] = "unknown matsim action \"" + action + "\"";
        return;
    }
    echo(out);   // every response carries ledger, cells_per_m and box
}

void MatSimLab::rebuildFaces() {
    faces_.clear();
    facesCut_ = 0;
    facesDirty_ = false;
    if (!core_.created()) return;
    const double h = 1.0 / core_.cellsPerM();
    const uint32_t F = core_.full();
    const IVec3 o = core_.origin(), s = core_.size();
    auto& reg = Core::MaterialRegistry::instance();
    auto texName = [](uint8_t m) -> const char* {
        switch (m) {
            case Mat::Water: return "Ice";   // no Water material in the voxel shader; the FLIP debug draw's stand-in
            case Mat::Sand: return "Sand";
            case Mat::Snow: return "Snow";
            case Mat::Gravel: return "Gravel";
            default: return "Default";
        }
    };
    // Neighbour offsets in faceID order: 0 +Z, 1 -Z, 2 +X, 3 -X, 4 +Y, 5 -Y (dynamic_voxel.vert).
    static const int kN[6][3] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    for (int y = 0; y < s.y; ++y)
        for (int z = 0; z < s.z; ++z)
            for (int x = 0; x < s.x; ++x) {
                const IVec3 w{o.x + x, o.y + y, o.z + z};
                const Cell c = core_.at(w);
                const uint8_t m = cellMaterial(c);
                if (m == kEmpty || m == kSolid || cellFill(c) == 0) continue;
                const double frac = double(cellFill(c)) / double(F);
                for (int f = 0; f < 6; ++f) {
                    const Cell nb = core_.at({w.x + kN[f][0], w.y + kN[f][1], w.z + kN[f][2]});
                    const uint8_t nm = cellMaterial(nb);
                    bool hidden = false;
                    if (nm == kSolid) hidden = true;                                          // walls (and the box's outside)
                    else if (f == 4) hidden = cellFill(c) == F && cellFill(nb) > 0;           // full below a non-empty cell
                    else if (f == 5) hidden = cellFill(nb) == F;                              // resting on a full cell
                    else hidden = nm != kEmpty && cellFill(nb) >= cellFill(c);                // a side covered by a taller neighbour
                    if (hidden) continue;
                    if (faces_.size() >= kMaxFaces) { ++facesCut_; continue; }
                    DynamicSubcubeInstanceData d{};
                    d.worldPosition = glm::vec3(float((w.x + 0.5) * h), float((w.y + 0.5 * frac) * h), float((w.z + 0.5) * h));
                    d.textureIndex = reg.getTextureIndex(texName(m), f);
                    d.faceID = uint32_t(f);
                    d.scale = glm::vec3(float(h), float(frac * h), float(h));
                    d.rotation = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                    d.localPosition = glm::ivec3(0);
                    d.reserved2 = 3u;   // full skylight, as the FLIP debug draw
                    faces_.push_back(d);
                }
            }
}

} // namespace Phyxel::Core::MatSim
