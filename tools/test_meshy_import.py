#!/usr/bin/env python3
"""Self-test for tools/meshy_import.py - NO external service, NO engine.

Every input is a SYNTHETIC mesh built here with trimesh (their dimensions are
test fixtures, not real-world claims).  What this proves: the offline path
(mesh -> voxels -> materials -> file) is internally consistent at the three
tiers; the reduced GLB copy is a valid, smaller mesh; and the generation
BOOKKEEPING (request bodies, ledger, budget floor, key loading, batch resume)
behaves, with the three network functions replaced by a stub.  What it does
NOT prove: that the Meshy service accepts those request bodies, anything about
real Meshy meshes, or how a template looks in the engine.

Run either way:
    python tools/test_meshy_import.py          # prints each check + the RED control
    python -m pytest tools/test_meshy_import.py -q

Scratch output goes to $MESHY_IMPORT_SCRATCH (default: <tmp>/phyxel_meshy_import_test).
"""
import datetime
import json
import math
import os
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import meshy_import as mi  # noqa: E402
import asset_index  # noqa: E402  (read-only use: parse_template)

SCRATCH = os.environ.get("MESHY_IMPORT_SCRATCH") or os.path.join(
    tempfile.gettempdir(), "phyxel_meshy_import_test")
MESH_DIR = os.path.join(SCRATCH, "meshes")
OUT_DIR = os.path.join(SCRATCH, "out")

# SAFETY: nothing in this file may reach the Meshy service, the owner's key, the
# real ledger or the real reference folder. The API tests replace the three
# network functions with a stub; these lines make any call that slipped past
# the stub fail locally instead.
mi.MESHY_BASE = "http://127.0.0.1:9/meshy-test-must-not-reach-the-network"
mi.ENV_FILE = os.path.join(SCRATCH, "no_such.env")
mi.LEDGER_PATH = os.path.join(SCRATCH, "default_ledger.jsonl")
mi.MESHY_REF_ROOT = os.path.join(SCRATCH, "default_ref_root")
os.environ.pop(mi.MESHY_KEY_ENV, None)

# fixture colours = exact materials.json colorTint values, so the expected
# nearest material is unambiguous
WALL = (217, 199, 140)     # Sandstone
ROOF = (184, 102, 66)      # ClayTile
WINDOW = (77, 87, 102)     # Slate
DOOR = (115, 64, 26)       # WoodWalnut
STAVE = (140, 97, 46)      # Log
HOOP = (179, 179, 204)     # Metal
BRICK = (191, 89, 64)      # Bricks
STONE = (153, 128, 102)    # Stone

HOUSE_W, HOUSE_D, HOUSE_EAVES, HOUSE_RISE = 6.0, 8.0, 5.4, 2.0
HOUSE_RIDGE = HOUSE_EAVES + HOUSE_RISE
HOUSE_PITCH_DEG = math.degrees(math.atan2(HOUSE_RISE, HOUSE_W / 2))   # 33.69


# --------------------------------------------------------------------------
# synthetic meshes
# --------------------------------------------------------------------------

class _Builder:
    """Unshared-vertex mesh builder: every patch carries one flat vertex colour
    (colour painted on a single surface, like a Meshy texture)."""

    def __init__(self):
        self.v, self.f, self.c = [], [], []

    def poly(self, pts, colour):
        b = len(self.v)
        self.v += [list(p) for p in pts]
        self.c += [list(colour) + [255]] * len(pts)
        for i in range(1, len(pts) - 1):
            self.f.append([b, b + i, b + i + 1])

    def mesh(self):
        import trimesh
        return trimesh.Trimesh(vertices=np.array(self.v, float), faces=np.array(self.f),
                               vertex_colors=np.array(self.c, np.uint8), process=False)


def _wall_patches(b, to3d, u_len, v_len, rects):
    """A u x v rectangular wall split into patches; rects = [(u0,u1,v0,v1,colour)]."""
    us = sorted({0.0, u_len} | {r[0] for r in rects} | {r[1] for r in rects})
    vs = sorted({0.0, v_len} | {r[2] for r in rects} | {r[3] for r in rects})
    for i in range(len(us) - 1):
        for j in range(len(vs) - 1):
            u0, u1, v0, v1 = us[i], us[i + 1], vs[j], vs[j + 1]
            colour = WALL
            for (a0, a1, c0, c1, col) in rects:
                if u0 >= a0 and u1 <= a1 and v0 >= c0 and v1 <= c1:
                    colour = col
            b.poly([to3d(u0, v0), to3d(u1, v0), to3d(u1, v1), to3d(u0, v1)], colour)


def make_house(path):
    """Gabled two-storey block: 6 (x) x 8 (z) m plan, eaves 5.4 m, ridge 7.4 m along
    Z, two window rows on both long walls, a door on the +Z gable end."""
    b = _Builder()
    W, D, E, R = HOUSE_W, HOUSE_D, HOUSE_EAVES, HOUSE_RIDGE
    wins = [(zc - 0.5, zc + 0.5, y0, y0 + 1.0, WINDOW)
            for zc in (1.5, 4.0, 6.5) for y0 in (0.9, 3.6)]
    _wall_patches(b, lambda u, v: (0.0, v, u), D, E, wins)          # x = 0
    _wall_patches(b, lambda u, v: (W, v, u), D, E, wins)            # x = W
    _wall_patches(b, lambda u, v: (u, v, 0.0), W, E, [])            # z = 0
    _wall_patches(b, lambda u, v: (u, v, D), W, E, [(2.5, 3.5, 0.0, 2.0, DOOR)])  # z = D
    b.poly([(0, E, 0), (W, E, 0), (W / 2, R, 0)], WALL)             # gables
    b.poly([(0, E, D), (W, E, D), (W / 2, R, D)], WALL)
    b.poly([(0, E, 0), (W / 2, R, 0), (W / 2, R, D), (0, E, D)], ROOF)
    b.poly([(W, E, 0), (W / 2, R, 0), (W / 2, R, D), (W, E, D)], ROOF)
    b.poly([(0, 0, 0), (W, 0, 0), (W, 0, D), (0, 0, D)], WALL)      # floor
    b.mesh().export(path)


def make_barrel(path):
    """Barrel: r 0.30 x h 0.90 m staves + two hoops r 0.32 x h 0.06 m."""
    import trimesh
    rot = trimesh.transformations.rotation_matrix(-math.pi / 2, [1, 0, 0])   # Z axis -> Y

    def cyl(r, h, y, colour):
        m = trimesh.creation.cylinder(radius=r, height=h, sections=48)
        m.apply_transform(rot)
        m.apply_translation([0, y, 0])
        m.visual = trimesh.visual.ColorVisuals(
            m, vertex_colors=np.tile(np.array(list(colour) + [255], np.uint8),
                                     (len(m.vertices), 1)))
        return m
    parts = [cyl(0.30, 0.90, 0.45, STAVE), cyl(0.32, 0.06, 0.25, HOOP),
             cyl(0.32, 0.06, 0.65, HOOP)]
    trimesh.util.concatenate(parts).export(path)


def make_plate(path):
    """Thin board 0.8 x 0.6 x 0.02 m, NO colour, exported as OBJ."""
    import trimesh
    m = trimesh.creation.box(extents=[0.8, 0.6, 0.02])
    with open(path, "w", encoding="utf-8") as f:       # bare OBJ: v + f only
        for v in m.vertices:
            f.write("v %.6f %.6f %.6f\n" % tuple(v))
        for t in m.faces:
            f.write("f %d %d %d\n" % tuple(int(i) + 1 for i in t))


def make_textured_cube(path):
    """1 m cube, UV-mapped, texture = left half brick red / right half stone grey."""
    import trimesh
    from PIL import Image
    img = np.zeros((64, 64, 3), np.uint8)
    img[:, :32] = BRICK
    img[:, 32:] = STONE
    quads = [
        [(0, 0, 1), (1, 0, 1), (1, 1, 1), (0, 1, 1)], [(1, 0, 0), (0, 0, 0), (0, 1, 0), (1, 1, 0)],
        [(1, 0, 1), (1, 0, 0), (1, 1, 0), (1, 1, 1)], [(0, 0, 0), (0, 0, 1), (0, 1, 1), (0, 1, 0)],
        [(0, 1, 1), (1, 1, 1), (1, 1, 0), (0, 1, 0)], [(0, 0, 0), (1, 0, 0), (1, 0, 1), (0, 0, 1)],
    ]
    v, f, uv = [], [], []
    for q in quads:
        b = len(v)
        v += q
        uv += [(0, 0), (1, 0), (1, 1), (0, 1)]
        f += [[b, b + 1, b + 2], [b, b + 2, b + 3]]
    m = trimesh.Trimesh(vertices=np.array(v, float), faces=np.array(f), process=False)
    # explicit white baseColorFactor: glTF colour = texture x factor, and trimesh's
    # default material would export a grey (102) factor that darkens the texture
    mat = trimesh.visual.material.PBRMaterial(baseColorTexture=Image.fromarray(img),
                                              baseColorFactor=[255, 255, 255, 255])
    m.visual = trimesh.visual.TextureVisuals(uv=np.array(uv, float), material=mat)
    m.export(path)


_MESHES = {}


def mesh(name):
    if name not in _MESHES:
        os.makedirs(MESH_DIR, exist_ok=True)
        makers = {"house": ("house.glb", make_house), "barrel": ("barrel.glb", make_barrel),
                  "plate": ("plate.obj", make_plate), "texcube": ("texcube.glb", make_textured_cube)}
        fn, maker = makers[name]
        p = os.path.join(MESH_DIR, fn)
        maker(p)
        _MESHES[name] = p
    return _MESHES[name]


_RUNS = {}


def run(mesh_name, tag, **kw):
    key = (mesh_name, tag)
    if key not in _RUNS:
        kw.setdefault("out_dir", OUT_DIR)
        kw.setdefault("check_library", True)
        _RUNS[key] = mi.import_mesh(mesh(mesh_name), f"{mesh_name}_{tag}", log=lambda *a: None, **kw)
    return _RUNS[key]


# --------------------------------------------------------------------------
# the assertions (operate on the WRITTEN FILE, read back)
# --------------------------------------------------------------------------

def assert_size(voxel_path, tier, expected_m):
    """The file's voxel bounding box equals expected_m (x,y,z metres) within ONE
    voxel of the tier on every axis."""
    back = mi.read_voxel_file(voxel_path)
    pitch = 1.0 / mi.TIER_RES[tier]
    got = [c / back["grid_res"] for c in mi.extent_cells(back["cells"])]
    for ax, g, e in zip("xyz", got, expected_m):
        assert abs(g - e) <= pitch + 1e-9, (
            f"SIZE MISMATCH {os.path.basename(voxel_path)} axis {ax}: voxel bbox {g:.4f} m vs "
            f"expected {e:.4f} m (tolerance one {tier} voxel = {pitch:.4f} m)")
    return got


def assert_file_consistent(r, tier):
    """voxels > 0; materials exist; the file parses back to exactly the grid the
    importer built; asset_index's reader agrees on counts/materials/class."""
    path = r["files"]["voxel"]
    names = mi.load_material_names()
    back = mi.read_voxel_file(path)
    assert len(back["cells"]) > 0, "no voxels"
    mats = set(back["cells"].values())
    assert mats <= names, f"unknown materials {mats - names}"

    grid = r["grid"]
    k = back["grid_res"] // mi.TIER_RES[tier]         # 3 for subcube files (9-lattice reader)
    want = {}
    for x, y, z in zip(*np.nonzero(grid >= 0)):
        want[(int(x), int(y), int(z))] = r["clusters"][int(grid[x, y, z])]["material"]
    got = {(x // k, y // k, z // k): m for (x, y, z), m in back["cells"].items()}
    assert len(back["cells"]) == len(want) * k ** 3, (len(back["cells"]), len(want), k)
    assert got == want, "file does not parse back to the importer's grid"

    info = asset_index.parse_template(path)
    assert info["total"] == sum(back["counts"].values())
    assert set(info["materials"]) == mats
    expect_class = {"fine": "fine-81", "micro": "microcube", "subcube": "subcube"}[tier]
    if tier == "micro" and back["counts"]["M"] == 0:
        expect_class = "subcube"
    assert info["resolution_class"] == expect_class, info["resolution_class"]
    assert r["lint_problems"] == [], r["lint_problems"]
    return back


BARREL_EXT = (0.64, 0.90, 0.64)
HOUSE_EXT = (HOUSE_W, HOUSE_RIDGE, HOUSE_D)


def _scaled(ext, size, axis=1):
    s = size / ext[axis]
    return tuple(e * s for e in ext)


# --------------------------------------------------------------------------
# tests
# --------------------------------------------------------------------------

NOTES = {}


def noted(fn):
    """Keep a test's human-readable note out of its return value (pytest wants None)."""
    import functools

    @functools.wraps(fn)
    def wrapper():
        NOTES[fn.__name__] = fn()
    return wrapper


@noted
def test_barrel_all_tiers_size_and_roundtrip():
    for tier in ("fine", "micro", "subcube"):
        r = run("barrel", tier, kind="prop", tier=tier, size=0.9)
        assert_file_consistent(r, tier)
        assert_size(r["files"]["voxel"], tier, BARREL_EXT)
        # the 6 cm hoops survive at 1.2 cm and 11 cm voxels; at 33 cm they are
        # out-voted inside their voxel (a limit of the tier, reported in the docs)
        want = {"Log"} if tier == "subcube" else {"Log", "Metal"}
        assert {c["material"] for c in r["clusters"]} == want, r["clusters"]


@noted
def test_house_all_tiers_size_and_roundtrip():
    for tier, size in (("fine", 0.6), ("micro", HOUSE_RIDGE), ("subcube", HOUSE_RIDGE)):
        r = run("house", tier, kind="building", tier=tier, size=size)
        assert_file_consistent(r, tier)
        assert_size(r["files"]["voxel"], tier, _scaled(HOUSE_EXT, size))
        mats = {c["material"] for c in r["clusters"]}
        assert {"Sandstone", "ClayTile"} <= mats, mats


@noted
def test_size_control_is_red():
    """CONTROL: the size assertion must FAIL when the expectation is wrong. The
    barrel was imported at 0.9 m; claiming 1.8 m must raise."""
    r = run("barrel", "micro", kind="prop", tier="micro", size=0.9)
    try:
        assert_size(r["files"]["voxel"], "micro", _scaled(BARREL_EXT, 1.8))
    except AssertionError as e:
        return str(e)
    raise RuntimeError("size control did NOT fail - assert_size measures nothing")


@noted
def test_wrong_size_request_is_red():
    """CONTROL 2: an import run at the WRONG size (1.2 m) fails the 0.9 m check."""
    r = run("barrel", "wrong", kind="prop", tier="micro", size=1.2)
    try:
        assert_size(r["files"]["voxel"], "micro", BARREL_EXT)
    except AssertionError as e:
        return str(e)
    raise RuntimeError("wrong-size import passed the size check")


@noted
def test_solid_vs_shell():
    solid = run("barrel", "fine", kind="prop", tier="fine", size=0.9)
    shell = run("barrel", "fine_shell", kind="prop", tier="fine", size=0.9, fill="shell")
    assert shell["voxels"]["interior"] == 0
    assert solid["voxels"]["interior"] > 0
    assert solid["voxels"]["total"] > shell["voxels"]["total"]
    assert solid["voxels"]["surface"] == shell["voxels"]["surface"]
    # the fill did not leak: voxel volume ~ analytic volume of the three cylinders
    vol = math.pi * 0.30 ** 2 * 0.90 + 2 * math.pi * (0.32 ** 2 - 0.30 ** 2) * 0.06
    got = solid["voxels"]["total"] / 81 ** 3
    assert 0.97 * vol <= got <= 1.15 * vol, (got, vol)
    return f"solid {solid['voxels']['total']} vs shell {shell['voxels']['total']} voxels; " \
           f"solid volume {got:.4f} m3 vs analytic {vol:.4f} m3"


@noted
def test_thin_plate_no_colour():
    # no colour and no --default-material -> refused, not guessed
    try:
        mi.import_mesh(mesh("plate"), "plate_refused", kind="prop", size=0.6, out_dir=OUT_DIR,
                       log=lambda *a: None)
    except mi.ImportError_ as e:
        refusal = str(e)
    else:
        raise AssertionError("colourless mesh was mapped without --default-material")
    assert "--default-material" in refusal
    for fill in ("solid", "shell"):
        r = run("plate", fill, kind="prop", tier="micro", size=0.6, fill=fill,
                default_material="WoodPlanks")
        assert_file_consistent(r, "micro")
        assert_size(r["files"]["voxel"], "micro", (0.8, 0.6, 0.02))
        assert {c["material"] for c in r["clusters"]} == {"WoodPlanks"}
    # a 2 cm board is one micro voxel thick: nothing to fill, and it did not vanish
    a, b = run("plate", "solid"), run("plate", "shell")
    assert a["voxels"]["interior"] == 0 and a["voxels"]["total"] == b["voxels"]["total"] > 0
    assert a["dims_cells"][2] == 1
    return f"refusal: {refusal[:90]}...; plate {a['dims_cells']} cells, {a['voxels']['total']} voxels"


@noted
def test_textured_mesh_clusters():
    r = run("texcube", "micro", kind="prop", tier="micro", size=1.0)
    assert_file_consistent(r, "micro")
    assert_size(r["files"]["voxel"], "micro", (1.0, 1.0, 1.0))
    by = {c["material"]: c["surface_voxels"] for c in r["clusters"]}
    assert set(by) == {"Bricks", "Stone"}, by
    frac = by["Bricks"] / (by["Bricks"] + by["Stone"])
    assert 0.3 <= frac <= 0.7, frac
    return f"texture clusters {by} (brick share {frac:.2f})"


@noted
def test_overrides():
    r = run("house", "override", kind="building", tier="micro", size=HOUSE_RIDGE,
            map_overrides={"#4d5766": "Glass", "0": "StoneBricks"})
    mats = {c["material"]: c["how"] for c in r["clusters"]}
    assert mats.get("Glass") == "override" and mats.get("StoneBricks") == "override", mats
    assert "Slate" not in mats and "Sandstone" not in mats
    assert_file_consistent(r, "micro")
    try:
        mi.import_mesh(mesh("house"), "house_badmap", kind="building", size=HOUSE_RIDGE,
                       out_dir=OUT_DIR, map_overrides={"0": "stonebricks"}, log=lambda *a: None)
    except mi.ImportError_ as e:
        assert "case-sensitive" in str(e)
    else:
        raise AssertionError("unknown material name accepted")


@noted
def test_massing_report():
    r = run("house", "micro", kind="building", tier="micro", size=HOUSE_RIDGE)
    m = r["massing"]
    v = 1 / 9
    assert abs(m["footprint_w_x_m"] - HOUSE_W) <= v and abs(m["footprint_d_z_m"] - HOUSE_D) <= v
    assert abs(m["ridge_height_m"] - HOUSE_RIDGE) <= v
    # the 95%-layer eaves rule sits up to ~2 voxels high on a roof with no overhang
    assert abs(m["eaves_height_m"] - HOUSE_EAVES) <= 2 * v + 1e-9, m["eaves_height_m"]
    assert m["roof_pitch_axis"] == "x"
    assert abs(m["roof_pitch_deg_rise_over_run"] - HOUSE_PITCH_DEG) <= 4.0, m
    assert abs(m["roof_pitch_deg_mean_gradient"] - HOUSE_PITCH_DEG) <= 4.0, m
    assert m["storey_count_guess"] == 2, m["opening_rows_m"]
    assert "MEASURED FROM THE MODEL" in m["_note"]
    assert os.path.exists(r["files"]["massing"])
    return (f"footprint {m['footprint_w_x_m']} x {m['footprint_d_z_m']} m, eaves "
            f"{m['eaves_height_m']} (fixture {HOUSE_EAVES}), ridge {m['ridge_height_m']} "
            f"(fixture {HOUSE_RIDGE}), pitch {m['roof_pitch_deg_rise_over_run']} / "
            f"{m['roof_pitch_deg_mean_gradient']} deg (fixture {HOUSE_PITCH_DEG:.2f}), "
            f"opening rows {m['opening_rows_m']}, storey guess {m['storey_count_guess']}")


@noted
def test_massing_control_single_storey():
    """CONTROL for the storey detector: the same house with NO openings must not
    report storeys (null), so '2' above is coming from the window rows."""
    import trimesh
    p = os.path.join(MESH_DIR, "house_blank.glb")
    b = _Builder()
    W, D, E, R = HOUSE_W, HOUSE_D, HOUSE_EAVES, HOUSE_RIDGE
    _wall_patches(b, lambda u, v: (0.0, v, u), D, E, [])
    _wall_patches(b, lambda u, v: (W, v, u), D, E, [])
    _wall_patches(b, lambda u, v: (u, v, 0.0), W, E, [])
    _wall_patches(b, lambda u, v: (u, v, D), W, E, [])
    b.poly([(0, E, 0), (W, E, 0), (W / 2, R, 0)], WALL)
    b.poly([(0, E, D), (W, E, D), (W / 2, R, D)], WALL)
    b.poly([(0, E, 0), (W / 2, R, 0), (W / 2, R, D), (0, E, D)], ROOF)
    b.poly([(W, E, 0), (W / 2, R, 0), (W / 2, R, D), (W, E, D)], ROOF)
    b.poly([(0, 0, 0), (W, 0, 0), (W, 0, D), (0, 0, D)], WALL)
    b.mesh().export(p)
    r = mi.import_mesh(p, "house_blank", kind="building", size=HOUSE_RIDGE, out_dir=OUT_DIR,
                       log=lambda *a: None)
    assert r["massing"]["storey_count_guess"] is None, r["massing"]["opening_rows_m"]
    return "blank-walled house -> storey_count_guess None"


@noted
def test_refusals():
    out = []
    for kw, needle in (
        (dict(kind="item", tier="micro", size=0.5), "fine"),            # items are fine-voxel
        (dict(kind="prop", size=None), "--size"),                       # scale never guessed
        (dict(kind="prop", tier="fine", size=40.0), "cap"),             # refuses, no OOM
    ):
        try:
            mi.import_mesh(mesh("barrel"), "barrel_refuse", out_dir=OUT_DIR,
                           log=lambda *a: None, **kw)
        except mi.ImportError_ as e:
            assert needle in str(e), (needle, str(e))
            out.append(str(e)[:70])
        else:
            raise AssertionError(f"not refused: {kw}")
    # an existing library stem is refused (stems are unique library-wide)
    existing = sorted(mi.library_stems())[0]
    try:
        mi.import_mesh(mesh("barrel"), existing, kind="prop", size=0.9, out_dir=OUT_DIR,
                       stem_prefix=False, log=lambda *a: None)
    except mi.ImportError_ as e:
        assert "already exists" in str(e)
    else:
        raise AssertionError("duplicate stem accepted")
    # the API path refuses without the explicit spend flag (NO network call is made)
    assert mi.main(["--text", "a barrel", "--name", "x", "--size", "1"]) == 2
    return "; ".join(out)


@noted
def test_preview_and_provenance():
    r = run("house", "preview", kind="building", tier="micro", size=HOUSE_RIDGE, preview=True,
            source="meshy", prompt="synthetic test fixture (not a Meshy mesh)")
    txt = open(r["files"]["preview_txt"], encoding="utf-8").read()
    assert "--- front" in txt and "--- side" in txt and "--- top" in txt
    front = txt.split("--- side")[0].splitlines()[1:]
    assert front[0].count(".") > front[-1].count("."), "gable should narrow toward the ridge"
    if "preview_png" in r["files"]:
        from PIL import Image
        assert Image.open(r["files"]["preview_png"]).size[0] > 100
    prov = json.load(open(r["files"]["provenance"], encoding="utf-8"))
    for k in ("source", "prompt", "date", "tier", "size_m", "size_axis", "input_sha256"):
        assert prov.get(k) is not None, k
    assert prov["source"] == "meshy" and prov["verified_in_engine"] is False
    assert set(prov["catalog_intent"]) <= set(asset_index.PRESERVED) | set(), \
        set(prov["catalog_intent"]) - set(asset_index.PRESERVED)


@noted
def test_register_into_scratch_library():
    """--register against a SCRATCH library root (never the repo's)."""
    import shutil
    lib = os.path.join(SCRATCH, "fake_library")
    shutil.rmtree(lib, ignore_errors=True)
    os.makedirs(os.path.join(lib, "furniture"))
    with open(os.path.join(lib, "furniture", "stool.voxel"), "w") as f:
        f.write("S 0 0 0  0 0 0  Wood\n")
    saved = (asset_index.TPL, asset_index.CATALOG, asset_index.ITEMS_MANIFEST,
             asset_index.ITEMS_JSON)
    try:
        r = run("barrel", "reg", kind="prop", tier="micro", size=0.9, source="meshy",
                prompt="synthetic test fixture (not a Meshy mesh)")
        entry, problems = mi.register(r["files"]["voxel"], library_root=lib, log=lambda *a: None)
        assert problems == [], problems
        assert os.path.exists(os.path.join(lib, "generated", r["stem"] + ".voxel"))
        cat = json.load(open(os.path.join(lib, "template_catalog.json"), encoding="utf-8"))
        e = cat[r["stem"]]
        assert e["category"] == "generated" and e["source"] == "meshy"
        assert e["prompt"].startswith("synthetic") and e["resolution_class"] in ("microcube", "subcube")
        assert "stool" in cat
        # a plain asset_index regeneration keeps the PRESERVED provenance fields
        cat2, _ = asset_index.build_index()
        assert cat2[r["stem"]]["prompt"] == e["prompt"] and "meshy" in cat2[r["stem"]]["method"]
        dropped = "source" not in cat2[r["stem"]]
        try:
            mi.register(r["files"]["voxel"], library_root=lib, log=lambda *a: None)
        except mi.ImportError_:
            pass
        else:
            raise AssertionError("re-registering the same stem was accepted")
    finally:
        (asset_index.TPL, asset_index.CATALOG, asset_index.ITEMS_MANIFEST,
         asset_index.ITEMS_JSON) = saved
    return ("registered into scratch library; after a plain asset_index regeneration the "
            f"'source' key is {'DROPPED (not in asset_index.PRESERVED)' if dropped else 'kept'}"
            ", prompt/method/model/created are kept")


# --------------------------------------------------------------------------
# building materials, massing with a timber frame, reduced GLB, extra tiers
# --------------------------------------------------------------------------

# the six cluster colours of the first real Meshy building (2026-11-21,
# resources/reference/meshy/roman/meshyref_roman_strip_building_a.provenance.json)
# and the role the importer gave each on that model
REAL_CLUSTERS = [((228, 224, 209), "wall"), ((136, 45, 17), "roof"), ((175, 149, 125), "wall"),
                 ((172, 77, 45), "roof"), ((108, 93, 78), "wall"), ((50, 37, 27), "wall")]


def _fake_clusters():
    return [{"id": i, "rgb": list(rgb), "coloured": True, "surface_voxels": 1, "role": role}
            for i, (rgb, role) in enumerate(REAL_CLUSTERS)]


@noted
def test_building_material_set():
    names = mi.load_material_names()
    walls, roofs = mi.building_material_sets()
    # every name in the set exists (none invented), and none was dropped
    assert walls == mi.BUILDING_WALL_MATERIALS and roofs == mi.BUILDING_ROOF_MATERIALS
    assert set(walls + roofs) <= names
    # BEFORE (control): all materials + plain RGB reproduces the real run's bad picks
    before = mi.map_clusters(_fake_clusters(), mi.load_material_table(), {}, None, names)
    got_before = [c["material"] for c in before]
    assert got_before == ["Linen", "WoodWalnut", "LogPalm", "Wool", "WoodShingle", "LogSpruce"], \
        got_before
    # AFTER: the building set, roof clusters within the roof family
    rt = {"wall": mi.load_material_table(only=walls), "roof": mi.load_material_table(only=roofs)}
    after = mi.map_clusters(_fake_clusters(), mi.load_material_table(only=walls + roofs), {},
                            None, names, metric="chroma", role_tables=rt)
    for c in after:
        assert c["material"] in (roofs if c["role"] == "roof" else walls), c
    assert after[1]["material"] == "ClayTile" and after[3]["material"] == "ClayTile"
    assert "WEAK" in after[0]["how"], after[0]      # no plaster material: said, not hidden
    # through the importer: a building's materials stay inside the set, the roof is roof
    r = run("house", "micro", kind="building", tier="micro", size=HOUSE_RIDGE)
    for c in r["clusters"]:
        assert c["material"] in walls + roofs, c
    roof = [c for c in r["clusters"] if c["role"] == "roof"]
    assert [c["material"] for c in roof] == ["ClayTile"], r["clusters"]
    # CONTROL: --all-materials lets the walnut-coloured door leave the set
    r2 = run("house", "allmats", kind="building", tier="micro", size=HOUSE_RIDGE,
             all_materials=True, color_metric="rgb")
    assert "WoodWalnut" in {c["material"] for c in r2["clusters"]}
    # --only-materials still overrides the building set
    r3 = run("house", "only", kind="building", tier="micro", size=HOUSE_RIDGE,
             only_materials={"Bricks", "Slate"})
    assert {c["material"] for c in r3["clusters"]} <= {"Bricks", "Slate"}
    return ("before " + str(got_before) + " -> after "
            + str([c["material"] for c in after]))


def make_timber_house(path):
    """The fixture house with a timber frame painted on both long walls: corner and
    mid posts 0.3 m wide, a rail 0.3 m deep across the full wall between the storeys."""
    b = _Builder()
    W, D, E, R = HOUSE_W, HOUSE_D, HOUSE_EAVES, HOUSE_RIDGE
    frame = [(u0, u0 + 0.3, 0.0, E, STAVE) for u0 in (0.0, 2.6, 5.1, D - 0.3)]
    frame += [(0.0, D, 2.6, 2.9, STAVE)]
    wins = [(zc - 0.5, zc + 0.5, y0, y0 + 1.0, WINDOW)
            for zc in (1.5, 4.0, 6.5) for y0 in (0.9, 3.6)]
    for x in (0.0, W):
        _wall_patches(b, lambda u, v, x=x: (x, v, u), D, E, frame + wins)
    _wall_patches(b, lambda u, v: (u, v, 0.0), W, E, [])
    _wall_patches(b, lambda u, v: (u, v, D), W, E, [])
    b.poly([(0, E, 0), (W, E, 0), (W / 2, R, 0)], WALL)
    b.poly([(0, E, D), (W, E, D), (W / 2, R, D)], WALL)
    b.poly([(0, E, 0), (W / 2, R, 0), (W / 2, R, D), (0, E, D)], ROOF)
    b.poly([(W, E, 0), (W / 2, R, 0), (W / 2, R, D), (W, E, D)], ROOF)
    b.poly([(0, 0, 0), (W, 0, 0), (W, 0, D), (0, 0, D)], WALL)
    b.mesh().export(path)


@noted
def test_massing_timber_frame():
    """A painted timber frame must not be read as one tall opening row (the first real
    Meshy house reported 'opening_rows [[0, 10]], 1 storey' for two storeys)."""
    p = os.path.join(MESH_DIR, "house_timber.glb")
    os.makedirs(MESH_DIR, exist_ok=True)
    make_timber_house(p)
    r = mi.import_mesh(p, "house_timber", kind="building", size=HOUSE_RIDGE, out_dir=OUT_DIR,
                       log=lambda *a: None)
    m = r["massing"]
    assert m["storey_count_guess"] == 2, m["long_facades"]
    for side, f in m["long_facades"].items():
        assert len(f["opening_rows_m"]) == 2 and f["frame_clusters"], (side, f)
    assert abs(m["eaves_height_m"] - HOUSE_EAVES) <= 2 / 9 + 1e-9
    assert abs(m["eaves_height_95pct_layer_m"] - HOUSE_EAVES) <= 2 / 9 + 1e-9
    assert m["ridge_height_m"] <= m["top_height_m"]
    return f"timber-framed house -> rows {m['opening_rows_m']}, frame clusters " \
           f"{m['long_facades'][m['opening_rows_facade']]['frame_clusters']}, storey guess 2"


def make_textured_sphere(path):
    """Icosphere r 1 m, 20480 faces, UV from position, 256 px texture (upper half brick
    red, lower half stone grey)."""
    import trimesh
    from PIL import Image
    m = trimesh.creation.icosphere(subdivisions=5, radius=1.0)
    v = np.asarray(m.vertices)
    uv = np.c_[np.arctan2(v[:, 2], v[:, 0]) / (2 * math.pi) + 0.5, v[:, 1] * 0.5 + 0.5]
    img = np.zeros((256, 256, 3), np.uint8)
    img[:128] = BRICK          # image row 0 = v 1 = top of the sphere
    img[128:] = STONE
    mat = trimesh.visual.material.PBRMaterial(baseColorTexture=Image.fromarray(img),
                                              baseColorFactor=[255, 255, 255, 255])
    m.visual = trimesh.visual.TextureVisuals(uv=uv, material=mat)
    m.export(path)


@noted
def test_reduce_glb():
    import trimesh
    from PIL import Image
    src = os.path.join(MESH_DIR, "sphere.glb")
    os.makedirs(MESH_DIR, exist_ok=True)
    make_textured_sphere(src)
    dest = os.path.join(OUT_DIR, mi.reduced_name(src))
    os.makedirs(OUT_DIR, exist_ok=True)
    rep = mi.reduce_glb(src, dest, max_texture=64, max_faces=2000, log=lambda *a: None)
    assert dest.endswith("sphere.reduced.glb")
    assert rep["faces_before"] == 20480 and 500 <= rep["faces_after"] <= 2000, rep
    assert rep["bytes_after"] < rep["bytes_before"] / 4, rep
    assert rep["textures"] == [{"slot": "baseColorTexture", "from": [256, 256], "to": [64, 64],
                                "mime": "image/jpeg", "bytes": rep["textures"][0]["bytes"]}]
    # the copy is a valid GLB: it loads, keeps its extent and its texture
    back = trimesh.load(dest, process=False)
    g = list(back.geometry.values())[0]
    assert len(g.faces) == rep["faces_after"]
    assert np.abs(g.bounds - np.array([[-1, -1, -1], [1, 1, 1]])).max() < 0.1, g.bounds
    tex = g.visual.material.baseColorTexture
    assert tex.size == (64, 64) and tex.format == "JPEG"
    # ...and voxelizes to the same thing as the original (colours on the right halves)
    a = mi.import_mesh(src, "sphere_orig", kind="prop", size=2.0, out_dir=OUT_DIR,
                       log=lambda *x: None)
    b = mi.import_mesh(dest, "sphere_reduced", kind="prop", size=2.0, out_dir=OUT_DIR,
                       log=lambda *x: None)
    assert all(abs(x - y) <= 1 for x, y in zip(a["dims_cells"], b["dims_cells"]))
    for r in (a, b):
        by = {c["material"]: c for c in r["clusters"]}
        assert set(by) == {"Bricks", "Stone"}, by
        ys = {m: np.nonzero(r["grid"] == c["id"])[1].mean() for m, c in by.items()}
        assert ys["Bricks"] > ys["Stone"], ys           # brick is the upper half
    assert abs(a["voxels"]["total"] - b["voxels"]["total"]) <= 0.1 * a["voxels"]["total"]
    # a mesh already inside the budget keeps its faces; only the texture shrinks
    rep2 = mi.reduce_glb(mesh("texcube"), os.path.join(OUT_DIR, "texcube.reduced.glb"),
                         max_texture=32, max_faces=50000, log=lambda *x: None)
    assert rep2["faces_after"] == rep2["faces_before"] == 12
    assert rep2["textures"][0]["to"] == [32, 32]
    # alpha: an image with real transparency stays PNG, an opaque RGBA becomes JPEG
    rgba = np.full((80, 40, 4), 255, np.uint8)
    assert mi._shrink_image(Image.fromarray(rgba), 20, 85)[1:] == ("image/jpeg", (10, 20))
    rgba[:10, :, 3] = 0
    assert mi._shrink_image(Image.fromarray(rgba), 20, 85)[1:] == ("image/png", (10, 20))
    return (f"sphere {rep['bytes_before']:,} B / {rep['faces_before']} faces -> "
            f"{rep['bytes_after']:,} B / {rep['faces_after']} faces; voxels "
            f"{a['voxels']['total']} vs {b['voxels']['total']}")


@noted
def test_also_tier_and_default_reduce():
    out = os.path.join(SCRATCH, "also")
    spec = {"kind": "building", "size": HOUSE_RIDGE, "also_tier": ["subcube", "micro"]}
    r = mi.import_all(mesh("house"), "house_also", out, spec, log=lambda *a: None)
    assert [x["provenance"]["tier"] for x in r["extra"]] == ["subcube"]      # micro = primary
    sub = r["extra"][0]
    assert sub["stem"] == "meshyref_house_also_subcube"
    assert_file_consistent(sub, "subcube")
    big, small = (os.path.getsize(x["files"]["voxel"]) for x in (r, sub))
    assert small < big / 5, (big, small)
    # --reduce defaults ON for a building, and the provenance records both companions
    assert r["reduce"] and os.path.exists(os.path.join(out, "house.reduced.glb"))
    prov = json.load(open(r["files"]["provenance"], encoding="utf-8"))
    assert prov["also_tiers"] == [{"tier": "subcube", "stem": sub["stem"]}]
    assert prov["reduced_copy"]["file"] == "house.reduced.glb"
    for p in r["outputs"]:
        assert os.path.exists(p), p
    # ...and OFF for a prop
    r2 = mi.import_all(mesh("barrel"), "barrel_also", out, {"kind": "prop", "size": 0.9},
                       log=lambda *a: None)
    assert r2["reduce"] is None and "reduced_glb" not in r2["files"]
    return f"micro {big:,} B + subcube {small:,} B"


# --------------------------------------------------------------------------
# stray pieces (islands), era / note / size basis
# --------------------------------------------------------------------------

FIGURE = (0, 200, 0)       # a colour no part of the house has
FIG_X0, FIG_Z0 = HOUSE_W + 1.0, -1.5      # figure A beside the +x wall, B in front of z = 0


def _house_polys(b):
    W, D, E, R = HOUSE_W, HOUSE_D, HOUSE_EAVES, HOUSE_RIDGE
    wins = [(zc - 0.5, zc + 0.5, y0, y0 + 1.0, WINDOW)
            for zc in (1.5, 4.0, 6.5) for y0 in (0.9, 3.6)]
    _wall_patches(b, lambda u, v: (0.0, v, u), D, E, wins)
    _wall_patches(b, lambda u, v: (W, v, u), D, E, wins)
    _wall_patches(b, lambda u, v: (u, v, 0.0), W, E, [])
    _wall_patches(b, lambda u, v: (u, v, D), W, E, [(2.5, 3.5, 0.0, 2.0, DOOR)])
    b.poly([(0, E, 0), (W, E, 0), (W / 2, R, 0)], WALL)
    b.poly([(0, E, D), (W, E, D), (W / 2, R, D)], WALL)
    b.poly([(0, E, 0), (W / 2, R, 0), (W / 2, R, D), (0, E, D)], ROOF)
    b.poly([(W, E, 0), (W / 2, R, 0), (W / 2, R, D), (W, E, D)], ROOF)
    b.poly([(0, 0, 0), (W, 0, 0), (W, 0, D), (0, 0, D)], WALL)


def _box(b, x0, y0, z0, dx, dy, dz, colour):
    x1, y1, z1 = x0 + dx, y0 + dy, z0 + dz
    for q in ([(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0)],
              [(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)],
              [(x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0)],
              [(x1, y0, z0), (x1, y0, z1), (x1, y1, z1), (x1, y1, z0)],
              [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)],
              [(x0, y1, z0), (x1, y1, z0), (x1, y1, z1), (x0, y1, z1)]):
        b.poly(q, colour)


def make_house_with_figures(path, touching=False):
    """The fixture house plus two 'figures' (0.5 x 1.7 x 0.3 m boxes) in the SAME
    mesh: A stands 1 m off the +x wall, B 1.2 m in front of the z = 0 wall. With
    touching=True figure A stands against the +x wall instead (connected)."""
    b = _Builder()
    _house_polys(b)
    _box(b, HOUSE_W if touching else FIG_X0, 0.0, 3.0, 0.5, 1.7, 0.3, FIGURE)
    _box(b, 2.0, 0.0, FIG_Z0, 0.5, 1.7, 0.3, FIGURE)
    b.mesh().export(path)


def _figure_voxels(r):
    """Voxels of the result whose cluster colour is the figures' green."""
    ids = [c["id"] for c in r["clusters"] if c["coloured"]
           and sum((a - b) ** 2 for a, b in zip(c["rgb"], FIGURE)) < 40 ** 2]
    return int(sum((r["grid"] == i).sum() for i in ids))


@noted
def test_islands_dropped_for_buildings():
    v = 1 / 9
    p = os.path.join(MESH_DIR, "house_figures.glb")
    os.makedirs(MESH_DIR, exist_ok=True)
    make_house_with_figures(p)

    def imp(tag, **kw):
        kw.setdefault("size", HOUSE_RIDGE)
        return mi.import_mesh(p, "house_fig_" + tag, kind="building", out_dir=OUT_DIR,
                              log=lambda *a: None, **kw)
    plain = run("house", "micro", kind="building", tier="micro", size=HOUSE_RIDGE)
    r = imp("drop")
    m, isl = r["massing"], r["provenance"]["islands"]
    # the footprint is the HOUSE's, the figures are gone, and it says what it dropped
    assert abs(m["footprint_w_x_m"] - HOUSE_W) <= v and abs(m["footprint_d_z_m"] - HOUSE_D) <= v, m
    assert r["dims_cells"] == plain["dims_cells"], (r["dims_cells"], plain["dims_cells"])
    assert _figure_voxels(r) == 0
    assert isl["applied"] and isl["connectivity"] == 26 and isl["min_fraction"] == 0.05
    assert isl["components"] == 3 and isl["dropped_components"] == 2, isl
    assert isl["dropped_voxels"] == sum(isl["dropped_component_voxels"]) > 100, isl
    assert isl["components_after"] == 1 and isl["kept_detached_components"] == 0
    assert m["islands_dropped"].startswith("2 components"), m["islands_dropped"]
    # the massing is computed AFTER the filter: it equals the plain house's
    for k in ("footprint_w_x_m", "footprint_d_z_m", "footprint_area_m2", "eaves_height_m",
              "ridge_height_m", "roof_pitch_deg_rise_over_run", "storey_count_guess"):
        assert m[k] == plain["massing"][k], (k, m[k], plain["massing"][k])
    on_disk = json.load(open(r["files"]["provenance"], encoding="utf-8"))["islands"]
    assert on_disk["dropped_components"] == 2 and on_disk["dropped_voxels"] == isl["dropped_voxels"]
    assert_file_consistent(r, "micro")
    assert_size(r["files"]["voxel"], "micro", HOUSE_EXT)

    # CONTROL: --keep-islands -> the figures are in the grid and the footprint is wider
    k = imp("keep", keep_islands=True)
    km = k["massing"]
    assert _figure_voxels(k) > 100
    assert km["footprint_w_x_m"] >= FIG_X0 + 0.5 - v and km["footprint_d_z_m"] >= HOUSE_D - FIG_Z0 - v, km
    assert k["provenance"]["islands"] == {"applied": False, "reason": "--keep-islands"}
    # CONTROL: a threshold below the figures' share keeps them (the fraction is what decides)
    share = max(isl["dropped_component_voxels"]) / isl["largest_component_voxels"]
    f = imp("lowfrac", island_fraction=share / 4)
    assert f["provenance"]["islands"]["dropped_components"] == 0 and _figure_voxels(f) > 100
    assert f["provenance"]["islands"]["kept_detached_components"] == 2

    # --size is the BUILDING's: figure A widens x; with --size-axis x the kept house is
    # HOUSE_W wide and full height, the control is squeezed to HOUSE_W including the figure
    rx = imp("sx", size=HOUSE_W, size_axis="x")
    kx = imp("sx_keep", size=HOUSE_W, size_axis="x", keep_islands=True)
    assert abs(rx["massing"]["ridge_height_m"] - HOUSE_RIDGE) <= v, rx["massing"]["ridge_height_m"]
    squeezed = HOUSE_RIDGE * HOUSE_W / (FIG_X0 + 0.5)
    assert abs(kx["massing"]["top_height_m"] - squeezed) <= v, kx["massing"]["top_height_m"]

    # the subcube tier too (figures are thinner than a voxel there)
    s = imp("sub", tier="subcube")
    assert s["provenance"]["islands"]["dropped_components"] == 2 and _figure_voxels(s) == 0
    assert_size(s["files"]["voxel"], "subcube", HOUSE_EXT)

    # LIMIT (documented, not fixed): a figure TOUCHING the wall is one component with it
    pt = os.path.join(MESH_DIR, "house_figures_touching.glb")
    make_house_with_figures(pt, touching=True)
    t = mi.import_mesh(pt, "house_fig_touch", kind="building", size=HOUSE_RIDGE,
                       out_dir=OUT_DIR, log=lambda *a: None)
    ti = t["provenance"]["islands"]
    assert ti["components"] == 2 and ti["dropped_components"] == 1, ti
    assert _figure_voxels(t) > 50 and t["massing"]["footprint_w_x_m"] >= HOUSE_W + 0.5 - v

    # props are not filtered (the barrel's parts are one piece anyway; the key is absent)
    assert run("barrel", "micro", kind="prop", tier="micro", size=0.9)["provenance"]["islands"] is None
    return (f"dropped {isl['dropped_components']} of {isl['components']} components / "
            f"{isl['dropped_voxels']} voxels (sizes {isl['dropped_component_voxels']}, largest "
            f"{isl['largest_component_voxels']}); footprint {m['footprint_w_x_m']} x "
            f"{m['footprint_d_z_m']} m vs kept-islands control {km['footprint_w_x_m']} x "
            f"{km['footprint_d_z_m']} m; touching figure stays ({_figure_voxels(t)} voxels)")


@noted
def test_cli_era_note_size_basis_and_console():
    import contextlib
    import io
    p = os.path.join(MESH_DIR, "house_figures.glb")
    os.makedirs(MESH_DIR, exist_ok=True)
    make_house_with_figures(p)
    ref = _fresh("cli_ref")
    out = os.path.join(ref, "medieval", "sub")

    def cli(*extra):
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = mi.main([p, "--kind", "building", "--size", str(HOUSE_RIDGE), "--no-reduce",
                          "--no-library-check", "--ref-root", ref, *extra])
        assert rc == 0, buf.getvalue()[-400:]
        return buf.getvalue()

    def prov(d, stem):
        return json.load(open(os.path.join(d, stem + ".provenance.json"), encoding="utf-8"))

    # era inferred from --out = <ref root>/<era>/...; no basis given -> NOT STATED, printed
    txt = cli("--name", "cli_a", "--out", out)
    a = prov(out, "meshyref_cli_a")
    assert a["era"] == "medieval" and a["note"] is None and a["size_basis"] == "NOT STATED"
    assert "SIZE BASIS: NOT STATED" in txt
    line = [ln for ln in txt.splitlines() if ln.strip().startswith("islands:")]
    assert len(line) == 1 and "dropped 2 components" in line[0] and "26-connected" in line[0], line
    assert f"/ {a['islands']['dropped_voxels']} voxels" in line[0]
    mass = json.load(open(os.path.join(out, "meshyref_cli_a.massing.json"), encoding="utf-8"))
    assert mass["size_basis"] == "NOT STATED" and mass["size_m"] == HOUSE_RIDGE
    assert "# size basis:   NOT STATED" in open(os.path.join(out, "meshyref_cli_a.voxel"),
                                                 encoding="utf-8").read(2000)
    # explicit --era wins over the folder; --note and --size-basis are stored and printed
    txt = cli("--name", "cli_b", "--out", out, "--era", "Roman", "--also-tier", "subcube",
              "--note", "CC BY-SA 2.0, A. Author, https://example.invalid/p",
              "--size-basis", "fixture value, not a survey")
    b = prov(out, "meshyref_cli_b")
    assert b["era"] == "roman" and b["note"].startswith("CC BY-SA 2.0")
    assert b["size_basis"] == "fixture value, not a survey"
    assert "SIZE BASIS: fixture value, not a survey" in txt and "CC BY-SA 2.0" in txt
    assert prov(out, "meshyref_cli_b_subcube")["size_basis"] == b["size_basis"]   # extra tier too
    # outside any reference folder: no era
    other = _fresh("cli_other")
    cli("--name", "cli_c", "--out", other)
    assert prov(other, "meshyref_cli_c")["era"] is None
    # --keep-islands on the command line
    txt = cli("--name", "cli_d", "--out", other, "--keep-islands")
    assert "islands: filter OFF" in txt
    assert prov(other, "meshyref_cli_d")["islands"]["applied"] is False
    txt = cli("--name", "cli_e", "--out", other, "--island-fraction", "0.0001")
    assert "dropped 0 components" in txt
    assert mi.infer_era(os.path.join(ref, "edo")) is None                 # not the default root
    assert mi.infer_era(os.path.join(mi.MESHY_REF_ROOT, "edo", "x")) == "edo"
    assert mi.infer_era(mi.MESHY_REF_ROOT) is None
    return [ln.strip() for ln in txt.splitlines() if "islands:" in ln][0][:110]


@noted
def test_generation_era_note_and_relative_paths():
    """A single (non-batch) generation: era read from the output folder, the note and
    the size basis in ledger + provenance, a local image stored repo-relative."""
    d = _fresh("gen_meta")
    ledger = os.path.join(d, "ledger.jsonl")
    out = os.path.join(mi.MESHY_REF_ROOT, "medieval")          # scratch root (see SAFETY)
    import shutil
    shutil.rmtree(out, ignore_errors=True)
    # an image INSIDE the repository, given by absolute path (read only)
    png = sorted(f for f in os.listdir(mi.TEXTURE_SRC) if f.lower().endswith(".png"))[0]
    img_abs = os.path.join(mi.TEXTURE_SRC, png)
    img_rel = "resources/textures/source/" + png
    spec = {"kind": "prop", "size": 0.9, "note": "author X, CC0, https://example.invalid/i",
            "size_basis": "fixture"}
    with stubbed(FakeMeshy(5000)):
        r = mi.generate_and_import("image-to-3d", img_abs, "gen_meta_a", out, spec, None, 591,
                                   ledger, log=lambda *a: None)
    e = _ledger_lines(ledger)[-1]
    assert e["era"] == "medieval", e["era"]                    # was null before
    assert e["note"] == spec["note"] and e["size_basis"] == "fixture" and e["size_m"] == 0.9
    assert e["image"] == img_rel, e["image"]
    prov = r["provenance"]
    assert prov["era"] == "medieval" and prov["note"] == spec["note"]
    assert prov["image"] == img_rel and prov["meshy"]["image"] == img_rel
    assert prov["catalog_intent"]["prompt"] == "image: " + img_rel
    on_disk = open(r["files"]["provenance"], encoding="utf-8").read()
    root = os.path.abspath(mi.ROOT)
    assert root not in on_disk and root.replace(os.sep, "/") not in on_disk \
        and json.dumps(root)[1:-1] not in on_disk, "absolute repo path in provenance"
    assert img_rel in open(r["files"]["voxel"], encoding="utf-8").read(3000)
    # an explicit era wins; with neither, and an output folder elsewhere, era stays None
    with stubbed(FakeMeshy(5000)):
        mi.generate_and_import("text-to-3d", "p", "gen_meta_b", out,
                               {"kind": "prop", "size": 0.9, "default_material": "Wood"},
                               None, 591, ledger, era="norse", log=lambda *a: None)
        mi.generate_and_import("text-to-3d", "p", "gen_meta_c", os.path.join(d, "x"),
                               {"kind": "prop", "size": 0.9}, None, 591, ledger,
                               log=lambda *a: None)
    a, b = _ledger_lines(ledger)[-2:]
    assert a["era"] == "norse" and b["era"] is None and b["size_basis"] == "NOT STATED"
    assert mi.rel_ref("https://example.invalid/a.jpg") == "https://example.invalid/a.jpg"
    assert mi.rel_ref(None) is None
    return f"ledger era {e['era']!r}, image {e['image']!r}"


# --------------------------------------------------------------------------
# API path with the network replaced by a stub (no request leaves this process)
# --------------------------------------------------------------------------

class FakeMeshy:
    """Stands in for the three network functions of meshy_import. Charges the
    pricing-page costs at task creation, like the service's balance does."""

    def __init__(self, balance):
        self.balance = balance
        self.bodies, self.tasks, self.downloads, self.balance_reads = [], {}, 0, 0

    def create(self, path, body):
        if "FAIL" in (body.get("prompt") or ""):
            raise mi.ImportError_("Meshy POST -> HTTP 400: stub failure")
        self.bodies.append((path, body))
        if path == mi.IMAGE_PATH:
            cost = 30
            assert body["image_url"].startswith(("http", "data:image/"))
        elif body["mode"] == "preview":
            cost = 20
        else:
            cost = 10
            assert body["preview_task_id"] in self.tasks
        self.balance -= cost
        tid = f"stub-{len(self.tasks) + 1:03d}"
        self.tasks[tid] = {"id": tid, "status": "SUCCEEDED", "progress": 100,
                           "consumed_credits": cost, "model_urls": {"glb": "stub://" + tid},
                           "_src": "barrel" if path == mi.IMAGE_PATH and "data:" in
                                   body["image_url"] else "house"}
        return tid

    def get(self, path):
        if path == mi.BALANCE_PATH:
            self.balance_reads += 1
            return {"balance": self.balance, "monthly_credits": 0, "permanent_credits": 0}
        return self.tasks[path.rsplit("/", 1)[1]]

    def download(self, task, dest):
        import shutil
        self.downloads += 1
        shutil.copyfile(mesh(task["_src"]), dest)
        return dest


class stubbed:
    def __init__(self, fake):
        self.fake = fake

    def __enter__(self):
        self.saved = (mi._meshy_create, mi._meshy_get, mi._meshy_download)
        mi._meshy_create, mi._meshy_get, mi._meshy_download = \
            self.fake.create, self.fake.get, self.fake.download
        return self.fake

    def __exit__(self, *exc):
        mi._meshy_create, mi._meshy_get, mi._meshy_download = self.saved


def _ledger_lines(path):
    if not os.path.exists(path):
        return []
    return [json.loads(line) for line in open(path, encoding="utf-8") if line.strip()]


def _fresh(name):
    import shutil
    d = os.path.join(SCRATCH, name)
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    return d


@noted
def test_api_key_loading():
    d = _fresh("key")
    env_file = os.path.join(d, ".env")
    secret = "msy_TESTONLY_not_a_real_key"
    assert mi.load_api_key(env={"MESHY_API_KEY": " abc "}, env_file=env_file) == "abc"
    try:
        mi.load_api_key(env={}, env_file=env_file)
    except mi.ImportError_ as e:
        assert "MESHY_API_KEY" in str(e)
    else:
        raise AssertionError("no key and no .env accepted")
    for text in (f"MESHY_API_KEY={secret}\n", f"# c\nOTHER=1\nexport MESHY_API_KEY='{secret}'\n",
                 f'﻿MESHY_API_KEY="{secret}"\r\n'):
        with open(env_file, "w", encoding="utf-8") as f:
            f.write(text)
        assert mi.load_api_key(env={}, env_file=env_file) == secret
    assert mi.load_api_key(env={"MESHY_API_KEY": "fromenv"}, env_file=env_file) == "fromenv"
    with open(env_file, "w", encoding="utf-8") as f:
        f.write("MESHY_API_KEY=\n")
    try:
        mi.load_api_key(env={}, env_file=env_file)
    except mi.ImportError_ as e:
        assert secret not in str(e)
    else:
        raise AssertionError("empty key accepted")
    # the key is read in exactly one place (the Authorization header), nowhere else
    src = open(mi.__file__, encoding="utf-8").read()
    assert src.count("load_api_key()") == 1, "the key must be read in exactly one place"


@noted
def test_credit_estimates():
    d = mi.gen_defaults()

    def est(mode, **kw):
        return mi.estimate_credits(mode, {**d, **kw})
    assert est("text-to-3d") == 30                       # the real 2026-11-21 run cost 30
    assert est("text-to-3d", texture=False) == 20
    assert est("text-to-3d", ai_model="meshy-6-lite") == 15
    assert est("text-to-3d", texture_resolution="8k", geometry_resolution="4k") == 40
    assert est("image-to-3d") == 30
    assert est("image-to-3d", ai_model="meshy-6-lite") == 15
    assert est("image-to-3d", texture=False) == 20
    assert est("image-to-3d", texture_resolution="8k") == 35
    assert est("text-to-3d", ai_model="some-future-model") == mi.MESHY_COST_UNKNOWN
    assert mi.DEFAULT_BUDGET_FLOOR == 591


@noted
def test_generation_ledger_and_budget_floor():
    d = _fresh("gen")
    ledger = os.path.join(d, "ledger.jsonl")
    spec = {"kind": "building", "size": HOUSE_RIDGE, "also_tier": ["subcube"]}
    with stubbed(FakeMeshy(5880)) as fake:
        r = mi.generate_and_import("text-to-3d", "a synthetic test house", "gen_house",
                                   os.path.join(d, "roman"), spec, None, 591, ledger,
                                   era="roman", log=lambda *a: None)
    lines = _ledger_lines(ledger)
    assert len(lines) == 1
    e = lines[0]
    assert e["mode"] == "text-to-3d (preview + refine)" and e["prompt"] == "a synthetic test house"
    assert e["balance_before"] == 5880 and e["balance_after"] == 5850 and e["credits"] == 30
    assert e["estimated_credits"] == 30 and e["consumed_credits_reported"] == 30
    assert e["task_ids"] == ["stub-001", "stub-002"] and e["status"] == "ok"
    assert e["ai_model"] == "latest" and e["params"]["target_polycount"] == 30000
    assert e["era"] == "roman" and e["name"] == "gen_house"
    assert any(o.endswith("gen_house.meshy.glb") for o in e["outputs"])
    assert any(o.endswith("gen_house.meshy.reduced.glb") for o in e["outputs"])
    assert any(o.endswith("meshyref_gen_house_subcube.voxel") for o in e["outputs"])
    assert datetime.datetime.fromisoformat(e["when"]).tzinfo is not None
    # the request bodies carry the size-controlling parameters, by their documented names
    (p1, preview), (p2, refine) = fake.bodies
    assert p1 == p2 == "/openapi/v2/text-to-3d"
    assert preview == {"mode": "preview", "prompt": "a synthetic test house",
                       "ai_model": "latest", "target_formats": ["glb"], "should_remesh": True,
                       "topology": "triangle", "target_polycount": 30000}, preview
    assert refine == {"mode": "refine", "preview_task_id": "stub-001",
                      "target_formats": ["glb"], "enable_pbr": False}, refine
    assert r["provenance"]["meshy"]["task_ids"] == ["stub-001", "stub-002"]

    # budget floor: 620 - 30 = 590 < 591 -> refused BEFORE any task is created
    with stubbed(FakeMeshy(620)) as fake:
        try:
            mi.generate_and_import("text-to-3d", "x", "gen_refused", os.path.join(d, "roman"),
                                   spec, None, 591, ledger, log=lambda *a: None)
        except mi.BudgetStop as stop:
            msg = str(stop)
        else:
            raise AssertionError("generation below the budget floor was not refused")
        assert fake.bodies == [] and fake.downloads == 0 and fake.balance == 620
    assert len(_ledger_lines(ledger)) == 1                     # a refusal spends nothing
    # CONTROL: 621 - 30 = 591 is exactly the floor -> allowed
    with stubbed(FakeMeshy(621)) as fake:
        mi.generate_and_import("text-to-3d", "y", "gen_at_floor", os.path.join(d, "roman"),
                               spec, None, 591, ledger, log=lambda *a: None)
        assert fake.balance == 591
    assert [x["credits"] for x in _ledger_lines(ledger)] == [30, 30]

    # a failure after the balance check still leaves a ledger line
    with stubbed(FakeMeshy(5000)) as fake:
        try:
            mi.generate_and_import("text-to-3d", "FAIL please", "gen_fail",
                                   os.path.join(d, "roman"), spec, None, 591, ledger,
                                   log=lambda *a: None)
        except mi.ImportError_:
            pass
        else:
            raise AssertionError("stub failure not raised")
    last = _ledger_lines(ledger)[-1]
    assert last["status"].startswith("FAILED") and last["credits"] == 0 and last["outputs"] == []

    # meshy-6-lite: remove_lighting=false is sent (the docs say true returns 400 there);
    # image-to-3d body; no remesh
    with stubbed(FakeMeshy(5000)) as fake:
        mi.generate_and_import("image-to-3d", "https://example.invalid/ref.jpg", "gen_img",
                               os.path.join(d, "roman"), spec,
                               {"ai_model": "meshy-6-lite", "should_remesh": False},
                               591, ledger, log=lambda *a: None)
        path, body = fake.bodies[0]
    assert path == "/openapi/v1/image-to-3d"
    assert body == {"image_url": "https://example.invalid/ref.jpg", "ai_model": "meshy-6-lite",
                    "should_texture": True, "target_formats": ["glb"], "should_remesh": False,
                    "enable_pbr": False, "remove_lighting": False}, body
    assert _ledger_lines(ledger)[-1]["image"] == "https://example.invalid/ref.jpg"
    return "refusal: " + msg


@noted
def test_batch_resumable_and_stops_at_floor():
    from PIL import Image
    d = _fresh("batch")
    ref, ledger = os.path.join(d, "ref"), os.path.join(d, "ledger.jsonl")
    Image.new("RGB", (40, 40), (120, 90, 60)).save(os.path.join(d, "barrel_ref.png"))
    manifest = os.path.join(d, "manifest.json")
    doc = {"defaults": {"era": "roman", "kind": "building", "size_axis": "y", "preview": False},
           "entries": [
               {"name": "house_a", "size": HOUSE_RIDGE, "prompt": "synthetic house a",
                "also_tier": ["subcube"], "note": "entry note a",
                "size_basis": "fixture ridge height"},
               {"name": "house_b", "size": HOUSE_RIDGE, "prompt": "synthetic house b",
                "map": {"0": "StoneBricks"}},
               {"era": "medieval", "name": "barrel_c", "kind": "prop", "tier": "micro",
                "size": 0.9, "image": "barrel_ref.png"},
               {"name": "house_d", "size": HOUSE_RIDGE, "prompt": "synthetic house d"}]}

    def write():
        with open(manifest, "w", encoding="utf-8") as f:
            json.dump(doc, f)

    def quiet(*a):
        pass
    write()

    # dry run: nothing is requested, nothing is written
    with stubbed(FakeMeshy(10_000)) as fake:
        s = mi.run_batch(manifest, ref, 591, ledger, dry_run=True, log=quiet)
        assert fake.bodies == [] and fake.balance_reads == 0
    assert [p["estimated_credits"] for p in s["planned"]] == [30, 30, 30, 30]
    assert not os.path.exists(ref) and not os.path.exists(ledger)
    # the CLI refuses a batch without the spend flag
    assert mi.main(["--batch", manifest, "--ref-root", ref, "--ledger", ledger]) == 2

    # balance 591 + 30 + 30 + 10: two generations fit, the third would cross the floor
    with stubbed(FakeMeshy(661)) as fake:
        s = mi.run_batch(manifest, ref, 591, ledger, log=quiet)
        assert fake.balance == 601 and len(fake.bodies) == 4      # 2 x (preview + refine)
    assert [x["entry"] for x in s["done"]] == ["roman/house_a", "roman/house_b"]
    assert s["stopped"] and "591" in s["stopped"] and s["failed"] is None
    assert s["remaining"] == ["medieval/barrel_c", "roman/house_d"]
    stop_msg = s["stopped"]
    assert os.path.exists(os.path.join(ref, "roman", "meshyref_house_a.provenance.json"))
    assert os.path.exists(os.path.join(ref, "roman", "meshyref_house_a_subcube.voxel"))
    assert not os.path.exists(os.path.join(ref, "medieval"))
    prov_b = json.load(open(os.path.join(ref, "roman", "meshyref_house_b.provenance.json"),
                            encoding="utf-8"))
    assert prov_b["clusters"][0]["material"] == "StoneBricks"      # the manifest's map
    # per-entry note / size_basis / era reach the provenance (both tiers) and the ledger
    for stem in ("meshyref_house_a", "meshyref_house_a_subcube"):
        pa = json.load(open(os.path.join(ref, "roman", stem + ".provenance.json"),
                            encoding="utf-8"))
        assert (pa["era"], pa["note"], pa["size_basis"]) == \
            ("roman", "entry note a", "fixture ridge height"), stem
    assert (prov_b["era"], prov_b["note"], prov_b["size_basis"]) == ("roman", None, "NOT STATED")
    la, lb = _ledger_lines(ledger)
    assert (la["era"], la["note"], la["size_basis"]) == ("roman", "entry note a",
                                                         "fixture ridge height")
    assert lb["note"] is None and lb["size_basis"] == "NOT STATED"
    assert [x["credits"] for x in _ledger_lines(ledger)] == [30, 30]

    # resume with more credits: the finished entries are skipped, not regenerated
    with stubbed(FakeMeshy(5000)) as fake:
        s = mi.run_batch(manifest, ref, 591, ledger, log=quiet)
        assert [b[0] for b in fake.bodies] == [mi.IMAGE_PATH, mi.TEXT_PATH, mi.TEXT_PATH]
        assert fake.bodies[0][1]["image_url"].startswith("data:image/png;base64,")
        assert fake.balance == 5000 - 30 - 30
    assert s["skipped"] == ["roman/house_a", "roman/house_b"]
    assert [x["entry"] for x in s["done"]] == ["medieval/barrel_c", "roman/house_d"]
    assert s["stopped"] is None and s["remaining"] == []
    assert os.path.exists(os.path.join(ref, "medieval", "meshy_barrel_c.voxel"))
    lines = _ledger_lines(ledger)
    assert len(lines) == 4 and lines[2]["era"] == "medieval" and "image" in lines[2]
    pc = json.load(open(os.path.join(ref, "medieval", "meshy_barrel_c.provenance.json"),
                        encoding="utf-8"))
    assert pc["era"] == "medieval" and pc["islands"] is None       # the entry's own era
    # a third run does nothing at all
    with stubbed(FakeMeshy(5000)) as fake:
        s = mi.run_batch(manifest, ref, 591, ledger, log=quiet)
        assert fake.bodies == [] and fake.balance_reads == 0 and len(s["skipped"]) == 4

    # a failing entry stops the batch (it may have spent credits) and is ledgered
    doc["entries"] = [{"name": "bad", "size": 5, "prompt": "FAIL"},
                      {"name": "after_bad", "size": 5, "prompt": "never reached"}]
    write()
    with stubbed(FakeMeshy(5000)) as fake:
        s = mi.run_batch(manifest, ref, 591, ledger, log=quiet)
        assert fake.bodies == []
    assert s["failed"]["entry"] == "roman/bad" and s["remaining"] == ["roman/after_bad"]
    assert _ledger_lines(ledger)[-1]["status"].startswith("FAILED")

    # malformed manifests are refused before any balance read
    for bad, needle in (({"name": "x", "size": 1, "prompt": "p", "image": "i.png"}, "exactly one"),
                        ({"name": "x", "prompt": "p"}, "'size'"),
                        ({"name": "x", "size": 1, "prompt": "p", "sizee": 2}, "unknown keys"),
                        ({"name": "x", "size": 1, "prompt": "p", "notes": "n"}, "unknown keys")):
        doc["entries"] = [bad]
        write()
        with stubbed(FakeMeshy(5000)) as fake:
            try:
                mi.run_batch(manifest, ref, 591, ledger, log=quiet)
            except mi.ImportError_ as e:
                assert needle in str(e), (needle, str(e))
            else:
                raise AssertionError(f"manifest accepted: {bad}")
            assert fake.balance_reads == 0
    # the shipped example manifest parses, and a dry run of it makes no request
    example = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           "meshy_manifest.example.json")
    with stubbed(FakeMeshy(0)) as fake:
        s = mi.run_batch(example, os.path.join(d, "ref_example"), 591, ledger, dry_run=True,
                         log=quiet)
        assert fake.bodies == [] and len(s["planned"]) >= 2
    return "stop message: " + stop_msg


TESTS = [
    test_barrel_all_tiers_size_and_roundtrip,
    test_house_all_tiers_size_and_roundtrip,
    test_size_control_is_red,
    test_wrong_size_request_is_red,
    test_solid_vs_shell,
    test_thin_plate_no_colour,
    test_textured_mesh_clusters,
    test_overrides,
    test_massing_report,
    test_massing_control_single_storey,
    test_refusals,
    test_preview_and_provenance,
    test_register_into_scratch_library,
    test_building_material_set,
    test_massing_timber_frame,
    test_reduce_glb,
    test_also_tier_and_default_reduce,
    test_islands_dropped_for_buildings,
    test_cli_era_note_size_basis_and_console,
    test_generation_era_note_and_relative_paths,
    test_api_key_loading,
    test_credit_estimates,
    test_generation_ledger_and_budget_floor,
    test_batch_resumable_and_stops_at_floor,
]


def main():
    print(f"scratch: {SCRATCH}")
    failed = 0
    for t in TESTS:
        try:
            t()
            note = NOTES.get(t.__name__)
        except Exception as e:  # noqa: BLE001
            failed += 1
            print(f"FAIL  {t.__name__}: {type(e).__name__}: {e}")
            continue
        red = "RED CONTROL (expected failure, raw message): " if t.__name__.endswith("_is_red") else ""
        print(f"PASS  {t.__name__}" + (f"\n      {red}{note}" if note else ""))
    print("\nper-run table (file, tier, cells, voxels, lines):")
    for (m, tag), r in sorted(_RUNS.items()):
        c = r["counts"]
        print(f"  {r['stem']:<26} {r['provenance']['tier']:<8} "
              f"{'x'.join(str(d) for d in r['dims_cells']):<12} "
              f"surface {r['voxels']['surface']:>7} interior {r['voxels']['interior']:>7}  "
              f"C={c['C']} S={c['S']} M={c['M']} V={c['V']}")
    print(f"\n{len(TESTS) - failed} passed, {failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
