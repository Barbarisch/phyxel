# Meshy import — mesh to voxel template (`tools/meshy_import.py`)

**Status (2026-11-21).** The offline path (mesh → voxels → materials → files) is self-tested on
synthetic meshes and has been run on one real Meshy mesh. The Meshy API path has been called
**once** for real (text-to-3D, preview + refine, 30 credits). The request parameters added since
that call, image-to-3D and the batch have **not** been sent to the service. Nothing this tool
produced has been spawned in the engine. Specified by [`WorldForgeV2Plan.md`](WorldForgeV2Plan.md)
§14; library rules in [`AssetLibrary.md`](AssetLibrary.md); the item format in
[`FineVoxelItems.md`](FineVoxelItems.md).

The tool turns a 3D mesh — normally one Meshy AI generated from a text prompt or from a reference
**image** of period architecture or a prop — into a `.voxel` template at a stated real size, with
materials from `resources/materials.json`, plus a provenance record. For a building it also writes
a massing report (footprint, eaves, ridge, roof pitch, a storey guess), a small subcube-tier stamp
on request, and a reduced copy of the mesh that is small enough to commit.

## What is tested and what is not

| | State |
|---|---|
| Local GLB / OBJ → voxels at `fine`, `micro`, `subcube` | tested, synthetic meshes (`tools/test_meshy_import.py`, 21 checks) |
| Size: voxel bounding box = requested size within one voxel, all three axes | tested, with two red controls (wrong expectation; wrong `--size`) |
| Solid vs shell fill; fill volume against the analytic volume | tested (barrel, 1.2 cm voxels: 0.274 m³ vs 0.259 m³) |
| Vertex-colour, textured (UV + image) and colourless meshes | tested, synthetic |
| Material names exist; file parses back to the importer's grid; `asset_index.parse_template` agrees | tested |
| Building material set (construction materials; roof clusters → roof family) | tested: the real run's six cluster colours, before and after; `--all-materials` control |
| Massing report | tested on a synthetic gabled house, a blank-walled control and a timber-framed variant; one real model recorded below (no known truth) |
| Reduced GLB (`--reduce`) | tested: loads back, extent kept, face budget met, texture downscaled, voxelizes to the same clusters; run on the real 45 MB mesh |
| `--also-tier` | tested |
| Ledger, budget floor, key loading, batch resume / stop / failure, request bodies | tested **with the three network functions replaced by a stub** — proves the bookkeeping, not that the service accepts the requests |
| `--register` | tested against a scratch library only, never the repository's |
| `lint_voxel_detail.py` on the outputs | run, no problems |
| **Meshy API, text-to-3D** | run once (2026-11-21) with the request bodies of that date; **the parameters added afterwards (`should_remesh`, `topology`, `target_polycount`, `enable_pbr`, …) have never been sent** |
| **Meshy API, image-to-3D; `--batch`; `GET /openapi/v1/balance` from this tool** | **NOT run against the service** |
| **In the engine** (spawn, screenshot, tint appearance, physics) | **NOT tested** |
| `asset_index.py --validate` over the real library with an imported template in it | NOT run |

## Usage

```
# a prop, 0.9 m tall, micro voxels, solid
python tools/meshy_import.py barrel.glb --name barrel --kind prop --size 0.9

# an item (fine voxels, 81 per metre), 0.75 m along its longest axis
python tools/meshy_import.py gladius.glb --name gladius --kind item --size 0.75 --size-axis longest \
       --map "0=Steel,1=Wood"

# a building REFERENCE: micro stamp + subcube stamp + massing + silhouettes + reduced GLB
python tools/meshy_import.py domus.glb --name roman_domus --kind building --size 7.4 --preview \
       --also-tier subcube --source meshy --image refs/domus_plate.jpg
```

Output goes to `--out` (default `<system temp>/phyxel_meshy_import`; a generation with `--era`
goes to `resources/reference/meshy/<era>/`), never into the template library:
`<stem>.voxel`, `<stem>.provenance.json`, for buildings `<stem>.massing.json`; with `--preview`
also `<stem>.preview.png` and `<stem>.preview.txt` (front / side / top silhouettes coloured by
cluster). The stem is `meshy_<name>`, or `meshyref_<name>` for a building, and is refused if any
template in the library already has it. The tool prints the size of every file it writes.

`--size` is required. It is the real extent in metres (1 engine cube = 1 m) along `--size-axis`
(`y` = height by default; `x`, `z`, `longest`). The mesh is scaled uniformly. The tool does not
infer a scale from the file, and it checks `--size` before any credit is spent.

### Tiers

| `--tier` | Cells per metre | Lines written | For | Default for |
|---|---|---|---|---|
| `fine` | 81 (1.23 cm) | `# grid: 81` + `V x y z Mat tint=` | items; kinematic-only, the engine refuses to bake them | `--kind item` (required) |
| `micro` | 9 (11.1 cm) | `M`, uniform 3×3×3 runs merged to `S` | props, furniture, architectural reference | `--kind prop`, `--kind building` |
| `subcube` | 3 (33.3 cm) | `S` | coarse reference stamps | — |

`furniture` and `architecture` are accepted as aliases of `micro`. Full cubes (`C`) are written
only with `--compact cube` (the default for buildings alone), and only for a uniform 9×9×9 block.

`--also-tier subcube` (repeatable) writes the same mesh again at another tier as
`<stem>_<tier>.voxel` with its own provenance and massing files. On the real building below the
micro stamp is 6.1 MB and the subcube stamp 0.59 MB.

### Fill

`--fill solid` is the default for items and props: the interior is flood-filled, so mass is right,
a broken prop is not hollow, and same-appearance cells merge into few boxes. `--fill shell` is the
default for `--kind building`: a reference stamp needs only the surface and is about a tenth the
size. Solid fill works on the voxel shell, so it does not need a watertight mesh, but an open
surface (a single plane) encloses nothing and gains no interior.

### Materials

1. Every surface sample's colour (texture through UVs × `baseColorFactor`, else vertex or face
   colour, else the material's flat colour) is clustered by k-means (`--clusters`, default 6;
   clusters closer than `--merge-dist` 30 in RGB are merged; fixed seed).
2. Each voxel takes the cluster most of its samples fall in.
3. Each cluster maps to the nearest candidate material. **The colour table is `materials.json`
   `physics.colorTint`** — the same table `vox_import.py` uses, not a hand table.
   `--color-table texture` uses the mean colour of each material's `side_n` source texture.
4. Overrides, when the mapping is wrong: `--map "0=Bricks,#4d5766=Glass"` (a cluster id from the
   printed table, or a hex colour that claims the nearest cluster), or a JSON sidecar `--map-file`
   (`<input>.map.json` is picked up automatically):
   `{"map": {"0": "Bricks"}, "default_material": "Stone"}`. An unknown material name is refused.
5. A mesh with no colour is refused unless `--default-material <Name>` is given.
6. Each line carries the cluster colour as `tint=#rrggbb` (`--emit material` leaves it off). The
   tint is per cluster, not per voxel, so fine items still merge into few boxes.

**Candidates for items and props:** every material with `category: "material"`, minus `vox_*`, the
emissives, `Default`, `Mirror`, `enchanted_log`, `burning_wood`, `LogHeartwood`; plain RGB distance.

**Candidates for `--kind building`** (default; the names are checked against `materials.json` when
the tool runs, and one that no longer exists is dropped with a note):

| Role | Materials |
|---|---|
| wall | `Stone`, `StoneBricks`, `Cobblestone`, `Bricks`, `Sandstone`, `WoodPlanks`, `Wood`, `Log`, `LogSpruce`, `LogPine`, `LogRedwood` |
| roof | `ClayTile`, `Thatch`, `WoodShingle`, `Slate`, `StoneSlab` |

- A cluster is a **roof** cluster when at least half its voxels lie above the measured eaves and
  it covers at least 10 % of the plan view above the eaves; it is then matched only against the
  roof family. Every other cluster is matched only against the wall list. (Detector thresholds.)
- Buildings use the **`chroma`** distance (`--color-metric`): lightness counts half, the
  red–green and yellow–blue differences count fully. Baked light and shade in a generated texture
  move lightness more than hue; with plain RGB the dark red roof of the real model below was
  nearer `WoodShingle` than `ClayTile`.
- A match whose distance is above 35 (`chroma`) or 60 (`rgb`) is printed as **`WEAK`**.
- Left out on purpose, and still reachable with `--map` or `--only-materials`: **`Glass`** (its
  `colorTint` is near-white, so it takes limewashed walls; windows in a Meshy texture are dark),
  `LogBirch`, `LogPalm`, `LogJungle` (pale barks that take plaster colours), `StoneTiles` (interior
  floor), `WoodWalnut`, cloth, metals, terrain.
- **`materials.json` has no plaster, daub or limewash material.** A white wall therefore has no
  good match; it lands on a grey stone with a white tint and is marked `WEAK`.
- `--only-materials a,b,c` replaces the building set; `--all-materials` restores the general
  candidate list; `--exclude` drops names from either.
- `WoodPlanks` and `Wood` share a `colorTint`; in the building set `WoodPlanks` is listed first
  and wins the tie. For items and props wood still resolves to `Wood`.

## Buildings are references

A Meshy building is a reference for the structure generator's rules and is never placed in a world
as generator output (CLAUDE.md provenance rule). `--kind building` therefore writes a stamp whose
header says so, tags it `reference, do-not-place`, and writes `<stem>.massing.json`:

| Field | How it is measured |
|---|---|
| `footprint_w_x_m`, `footprint_d_z_m`, `footprint_area_m2` | voxel bounding box and occupied columns |
| `roof_pitch_axis` | the horizontal axis along which the silhouette narrows in the top 30 % of the height; `null` when neither narrows (flat roof) |
| `eaves_height_m` | top of the highest layer whose silhouette extent across the pitch axis is ≥ 97 % of the widest layer (searched above 30 % of the ridge height): the height above which the building only narrows. With an overhanging roof this is the roof's lower edge, not the top of the wall |
| `eaves_height_95pct_layer_m` | the earlier rule, kept for comparison: top of the highest layer whose solid cross-section is ≥ 95 % of the largest layer |
| `ridge_height_m`, `top_height_m` | ridge = top of the highest layer that still spans ≥ 50 % of the building along the ridge axis (a chimney is not the ridge); top = the highest voxel |
| `roof_pitch_deg_rise_over_run` | (ridge − eaves) / half of `roof_span_across_pitch_axis_m`; assumes a symmetric roof |
| `roof_pitch_deg_mean_gradient` | atan of the mean column-top step along the pitch axis between eaves and ridge |
| `long_facades`, `opening_rows_m`, `storey_count_guess` | per long facade (first surface seen along the shorter plan axis, below the eaves): drop the dominant wall cluster; in every other cluster drop horizontal runs longer than a quarter of the facade (rails, band courses, plinth, roof edge), connected pieces wider than half the facade, and pieces three times taller than wide (posts); remove specks under about 0.3 m; count what is left per height. A row is a band ≥ 0.75 m where the count is ≥ 35 % of its peak, with dips under about 0.5 m bridged. The guess is the row count of the facade with more rows; `null` when none |

**Every number in that file is measured from the model at the `--size` given.** None of it is a
real-world source. A dimension typed into engine data still needs its own citation
(grounding-auditor). The thresholds above are detector parameters, not standards.

On the synthetic test house (fixture: 6 × 8 m, eaves 5.4, ridge 7.4, pitch 33.69°, two window rows)
the report reads 6.0 × 8.0, eaves 5.556, ridge 7.444, 32.2° / 33.1°, 2 storeys; the same house with
a painted timber frame also reads 2; with blank walls it reads `null`.

### Recorded case: the first real Meshy building (not a pass/fail test — no known truth)

`resources/reference/meshy/roman/roman_strip_building_a.meshy.glb`, imported with
`--kind building --size 24 --size-axis longest` at the micro tier (216 × 140 × 119 cells):

| | First import (the files in `resources/reference/meshy/roman/`) | Current tool, same GLB, same arguments |
|---|---|---|
| footprint | 24.0 × 13.222 m | 24.0 × 13.222 m |
| eaves | 10.0 m | 9.889 m (95 % rule: 10.0 m) |
| ridge / top | 15.556 m (highest voxel) | 15.444 m / 15.556 m |
| roof pitch | 43.6° / 42.3°, axis z | 42.3° / 42.6°, axis z |
| opening rows | `[[0.0, 10.0]]` | +z facade `[[1.556, 4.333], [6.889, 9.889]]`; −z facade `[[1.556, 4.444], [6.778, 9.889]]` |
| storey guess | 1 | 2 |

Read with care:

- The eaves figure did not really change. The previews show the roof's lower edge at about 9.9 m
  of 15.6 m, so 10.0 m was consistent with the model. The absolute numbers are large because
  `--size 24` (chosen for the long strip building that was asked for) stretched a two-storey house
  to 24 m long. Only the ratios carry over: eaves at 0.64 of the ridge height, pitch about 42°.
- The storey guess changed from 1 to 2 because the timber frame is no longer read as one opening
  that covers the wall. Two storeys agrees with the preview image. The upper row runs up to the
  eaves, so the row heights are not window heights.
- At the subcube tier the same model reads eaves 10.0 m, ridge 15.667 m, 41.8° / 43.2°, and 2
  storeys from one facade but 1 from the other.

## The reduced GLB (`--reduce`)

The raw Meshy download of the first building is 45.0 MB: 1,343,508 faces (39.8 MB of geometry) and
one 2048 × 2048 JPEG (5.3 MB). `--reduce` (default **on** for `--kind building`, `--no-reduce` to
skip, off for items and props unless asked) writes `<input name>.reduced.glb` into `--out` — next
to the original for a generation, where the download is `<name>.meshy.glb` and the copy
`<name>.meshy.reduced.glb`:

- every texture is downscaled so its longer edge is `--reduce-texture` (default 1024) and saved as
  JPEG (quality 85), or PNG when it has real transparency;
- geometry is decimated to about `--reduce-faces` (default 50,000) by **vertex clustering** (numpy
  only): vertices in one grid cell collapse to their mean; vertices of a cell that sit in different
  parts of the texture keep separate UVs at the same position, so texture seams survive. The cell
  size is found by bisection;
- vertex normals are dropped (a viewer then shades flat).

**The voxelization always reads the original, never the reduced copy.** On the real building:
45,024,596 B / 1,343,508 faces → **1,702,932 B / 49,382 faces**, texture 2048² → 1024² (429,886 B).
Voxelizing the reduced copy as a check gave a 216 × 139 × 119 grid (the original: 216 × 140 × 119),
eaves 10.0 m, ridge 15.444 m and 2 storeys, with a few pinholes in the roof. Vertex clustering is cruder than quadric decimation: silhouettes are
stair-stepped at the cell size (1.7 cm in the source's 1.9-unit frame, about 22 cm at 24 m). No
quadric simplifier is installed (`fast_simplification`, `open3d`, `pymeshlab` are all absent;
`trimesh.simplify_quadric_decimation` needs the first). Installing `fast-simplification` would give
a better copy; nothing was installed.

What to commit per generation, with the sizes from this case: the reduced GLB (1.7 MB), the subcube
stamp (0.59 MB), the previews, the massing and provenance files (under 50 KB together). The micro
stamp is 6.1 MB and the raw download 45 MB; the provenance file keeps the raw download's SHA-256
and the Meshy task ids. `.gitignore` does not exclude `*.meshy.glb` today.

## Registering an import (owner; modifies tracked files)

```
python tools/meshy_import.py --register "<out>/<stem>.voxel"
```

This copies the template and its sidecars into `resources/templates/generated/` (the quarantine
folder), seeds the catalog entry with `description`, `tags`, `method`, `model`, `created`,
`prompt`, regenerates `template_catalog.json` through `asset_index.build_index()`, and adds
`source` and `provenance` to the entry. Then run the remaining gates from §14:

```
python tools/lint_voxel_detail.py resources/templates/generated/<stem>.voxel
python tools/asset_index.py --validate
# spawn it (spawn_item for fine, spawn_template otherwise) and screenshot it at true scale
```

Curate (rename, move out of `generated/`) before a pipeline may use it.

**Gap:** `asset_index.py` keeps only the fields in its `PRESERVED` tuple, so the next plain
`python tools/asset_index.py` run drops `source` and `provenance` from the entry (the test shows
this). `prompt`, `method` (which contains `source: meshy`), `model` and `created` survive. Adding
`"source", "provenance"` to `PRESERVED` in `tools/asset_index.py` closes it; that file was not
edited.

## The Meshy API path

```
# one generation into resources/reference/meshy/roman/
python tools/meshy_import.py --text "<prompt>" --name strip_building_b --era roman \
       --kind building --size 10 --size-axis longest --also-tier subcube --preview \
       --yes-spend-credits

python tools/meshy_import.py --image-to-3d refs/strip_building.jpg --name strip_building_c ...
```

`--text` runs a preview task, then a refine task for the texture; `--image-to-3d` (a URL, or a
local JPG / PNG / WebP sent as a data URI) runs one task. Either downloads `model_urls.glb` as
`<name>.meshy.glb` and imports it. Both need `--yes-spend-credits`; without it the tool refuses
before any request.

**Key.** `MESHY_API_KEY` is read from the environment, else from a `MESHY_API_KEY=...` line in the
repository-root `.env` (gitignored). It is used in one place, the `Authorization` header, and is
never printed or written to a file.

**Parameters** (names, values and defaults from the Meshy docs, read 2026-11-21):

| Option | Request field | Default here | Docs |
|---|---|---|---|
| `--ai-model` | `ai_model` | `latest` (= Meshy 7.1). Also `meshy-7.1`, `meshy-6`, `meshy-6-lite`, `meshy-7` (deprecated, retires 2026-12-31) | text-to-3d, image-to-3d |
| (on) / `--no-remesh` | `should_remesh` | `true` — the service's own default is `false` for every model but `meshy-6-lite`, which is why the first download had 1.3 M faces | text-to-3d, image-to-3d |
| `--target-polycount` | `target_polycount` | `30000` (range 100–300000; used only when `should_remesh` is true) | text-to-3d, image-to-3d |
| `--topology` | `topology` | `triangle` (or `quad`) | text-to-3d, image-to-3d |
| `--geometry-resolution` | `geometry_resolution` | `standard` (`2k`, `4k`: +5 credits; sent only when not `standard`) | text-to-3d, image-to-3d |
| (on) / `--no-texture` | text: whether the refine task runs; image: `should_texture` | textured | text-to-3d, image-to-3d |
| `--texture-resolution` | `texture_resolution` | `2k` (the smallest; `4k`, `8k`; sent only when not `2k`) | text-to-3d (refine), image-to-3d |
| `--enable-pbr` | `enable_pbr` | `false` (no metallic / roughness / normal maps) | text-to-3d (refine), image-to-3d |
| `--texture-prompt` | `texture_prompt` | none | text-to-3d (refine), image-to-3d |
| `--remove-lighting` / `--no-remove-lighting` | `remove_lighting` | not sent, except `false` with `meshy-6-lite` (the docs say the service default `true` returns 400 with that model) | text-to-3d (refine), image-to-3d |
| always | `target_formats` | `["glb"]` | text-to-3d, image-to-3d |

Sources: <https://docs.meshy.ai/en/api/text-to-3d> (`POST /openapi/v2/text-to-3d`, `mode` `preview`
/ `refine`), <https://docs.meshy.ai/en/api/image-to-3d> (`POST /openapi/v1/image-to-3d`),
<https://docs.meshy.ai/en/api/remesh> (`POST /openapi/v1/remesh`, 5 credits — not used; the remesh
is asked for at generation), <https://docs.meshy.ai/en/api/balance> (`GET /openapi/v1/balance` →
`{"balance": N, ...}`), <https://docs.meshy.ai/en/api/pricing>.

**Credits** (pricing page, 2026-11-21): text-to-3D preview 20 (`latest`, `meshy-7.1`, `meshy-7`,
`meshy-6`) or 5 (`meshy-6-lite`); refine 10 at 2K / 4K, 15 at 8K; image-to-3D with 2K or 4K texture
30, or 15 with `meshy-6-lite` (2K only); 8K 35; mesh only 20 or 5; `geometry_resolution` 2k / 4k
+5. So a default text generation is 30 credits — which is what the first real run cost — and
`--ai-model meshy-6-lite` is 15. A model the table does not know is estimated at 40.

### Budget floor

`--budget-floor N` (default **591**). Before a generation the tool reads the balance and refuses
when `balance − estimated cost < N`; nothing is requested and no ledger line is written. 591 is the
owner's rule of 2026-11-21: the account held 5910 credits before the first call, at most 90 % of
that may be spent, so generation stops at 10 % = 591 credits remaining.

### Ledger

Every generation appends one JSON line to `resources/reference/meshy/ledger.jsonl` (created on
first use). A generation that fails after the balance check is ledgered too, with
`"status": "FAILED: …"`, because a failed call can still have been charged.

```json
{"when": "2026-11-21T17:46:21-05:00", "mode": "text-to-3d (preview + refine)",
 "name": "strip_building_b", "era": "roman", "kind": "building", "prompt": "…",
 "ai_model": "latest",
 "params": {"texture": true, "should_remesh": true, "topology": "triangle",
            "target_polycount": 30000, "geometry_resolution": "standard",
            "texture_resolution": "2k", "enable_pbr": false, "texture_prompt": null,
            "remove_lighting": null},
 "estimated_credits": 30, "task_ids": ["…", "…"], "balance_before": 5880, "status": "ok",
 "consumed_credits_reported": 30, "balance_after": 5850, "credits": 30,
 "outputs": ["resources/reference/meshy/roman/strip_building_b.meshy.glb", "…"]}
```

`credits` is the balance difference; `consumed_credits_reported` is the sum of the tasks'
`consumed_credits`. An image generation has `"image"` in place of `"prompt"`. Paths inside the
repository are written relative to its root. The first line of the existing ledger was written by
hand after the first real call (the tool had no ledger then) and has fewer fields.

### Batch

```
python tools/meshy_import.py --batch tools/meshy_manifest.example.json --dry-run
python tools/meshy_import.py --batch my_manifest.json --yes-spend-credits
```

A manifest is `{"defaults": {...}, "entries": [...]}` (or a bare list). Each entry has `era`,
`name`, `kind`, `size`, and exactly one of `prompt` or `image` (a URL, or a path relative to the
manifest or to the repository root); optional `tier`, `size_axis`, `also_tier`, `map` (the material
overrides), `default_material`, `clusters`, `preview`, `reduce`, `note`, and the generation
parameters under the field names in the table above (`ai_model`, `should_remesh`,
`target_polycount`, `topology`, `geometry_resolution`, `texture`, `texture_resolution`,
`enable_pbr`, `texture_prompt`, `remove_lighting`). Unknown keys are refused. Example:
[`tools/meshy_manifest.example.json`](../tools/meshy_manifest.example.json) — its sizes are
placeholders, not sourced dimensions.

- Output goes to `resources/reference/meshy/<era>/`.
- One generation at a time, in manifest order.
- An entry whose `<stem>.provenance.json` already exists is skipped, so a stopped batch is re-run
  with the same command.
- The batch stops cleanly when the next generation would cross the budget floor, and at the first
  failure (a failure may have spent credits). The printed summary lists what was done, skipped and
  left.
- `--dry-run` lists what would be generated and the estimated credits without any request.

## First real result (2026-11-21)

One text-to-3D generation, `ai_model: latest`, preview + refine, 30 credits (balance 5910 → 5880).
Files: `resources/reference/meshy/roman/`.

The prompt asked for a **single-storey** Roman provincial strip building with its **gable end to
the street**, a low stone plinth, limewashed wattle-and-daub walls, a red tegula roof and an open
timber **portico**. Meshy returned a **two-storey half-timbered house with its long side to the
front** and no portico — a north-European late-medieval house type. Text-to-3D did not follow the
period brief. It is of no use as a Roman reference, and it was imported at a size (24 m) chosen for
the building that was asked for. **Image-to-3D from sourced reference images is the route to try
next**; it has not been run.

What the run did establish: the endpoints, authentication, polling and download work; without
`should_remesh` the download is 45 MB with 1.3 M faces; nearest-colour mapping over all materials
gave a house `Linen` walls and a `WoodWalnut` and `Wool` roof.

Cluster table of that model, before (as imported) and after (current tool, same arguments):

| id | colour | voxels | Before | After | Role |
|---|---|---|---|---|---|
| 0 | `#e4e0d1` | 33,523 | `Linen` | `Cobblestone` — **WEAK** (44): limewash, no plaster material exists | wall |
| 1 | `#882d11` | 27,823 | `WoodWalnut` | `ClayTile` (27) | roof |
| 2 | `#af957d` | 26,359 | `LogPalm` | `Stone` (11) | wall |
| 3 | `#ac4d2d` | 25,863 | `Wool` | `ClayTile` (16) | roof |
| 4 | `#6c5d4e` | 18,636 | `WoodShingle` | `StoneBricks` (21) — these are the timber beams; `LogSpruce` was second at 24 | wall |
| 5 | `#32251b` | 4,732 | `LogSpruce` | `LogSpruce` (21) | wall |

The roof is now right. The limewash and the beams still need `--map` (for example
`--map "0=Sandstone,4=LogSpruce"`); colour alone cannot tell grey-brown timber from grey-brown
stone.

## Limits

- Surface voxelization marks every voxel the surface touches, so parts thinner than a voxel become
  one voxel thick, and a bounding box can read up to one voxel large.
- A feature smaller than a voxel is out-voted: the test barrel's 6 cm hoops survive at `fine` and
  `micro` and disappear at `subcube`.
- Voxels on a colour boundary go to whichever cluster has more samples, so boundaries are ragged by
  a voxel (visible in the preview).
- Caps: 16,000,000 grid cells and 12,000,000 surface samples; the tool refuses above them. A
  building at `fine` is refused.
- Colour matching compares one cluster colour against a small table. Baked lighting in a Meshy
  texture splits one material into several clusters; use `--map`.
- Glass, emissive parts and metal are identified by colour only; for buildings `Glass` is
  override-only.
- The roof / wall split needs a pitched roof; a flat roof is matched against the wall list.
- The storey guess needs openings drawn in a colour different from the wall, and reads the two
  long facades only.
- `--up z` exists for Z-up sources; GLB is Y-up. The front of the model is assumed to face +Z.

## Self-test

```
python tools/test_meshy_import.py            # prints each check and the red controls
python -m pytest tools/test_meshy_import.py -q
```

It builds synthetic meshes with `trimesh` (a gabled house with painted windows and a door, the same
house with a timber frame, a barrel with hoops, a colourless 2 cm board as OBJ, a UV-textured cube,
a 20,480-face textured sphere), runs the tool on them and checks the written files. The API tests
replace `_meshy_create`, `_meshy_get` and `_meshy_download` — the only three functions that touch
the network — with a stub, point the tool's base URL at a closed local port, and use a scratch
ledger, reference folder and `.env`. No request leaves the machine and no credit is spent. Scratch
output goes to `$MESHY_IMPORT_SCRATCH` or the system temp folder. The fixture dimensions are test
values, not real-world claims.
