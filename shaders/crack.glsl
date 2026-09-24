// crack.glsl — progressive fracture field for damaged voxels.
// docs/VoxelDamageVisualization.md §4. Included by voxel.frag.
//
// SINGLE SOURCE OF TRUTH for the crack pattern. Nothing else may re-implement it: the seam
// test (VoxelCrackSeamTest, doc §7) pins a CPU mirror of crackField() against a table of sampled values, and that
// guard only means anything while there is exactly one implementation to mirror.
//
// ── HARD RULE (doc §4): world position only ────────────────────────────────────────────────
// The only inputs are (worldPosAbs, faceNormal, stage01, style). This function must NEVER
// read sizeU, sizeV, texCoord, or any other chunk-derived quantity.
//
// Why, concretely: static_voxel.vert sets `uv = baseUV * vec2(sizeU, sizeV)`, so UV space is
// tied to the greedy-merged RECTANGLE, and merge runs are computed inside a 32³ loop and
// therefore TERMINATE AT CHUNK BORDERS. A crack seeded from uv would scale and repeat
// differently on either side of a chunk seam — chunk identity made visible, which is the
// exact failure `FeatureDesignKeys.md` is built around.
//
// Seeding from worldPosAbs instead buys a property nothing else could: the fracture network is
// continuous across voxel AND chunk boundaries, so a damaged wall reads as one cracked surface
// rather than N stamped decals. It also means a ⅓-scale sub-voxel face samples the same field
// as a full cube, so V2 needs no per-scale special-casing (doc §9).
//
// NOTE what is NOT continuous: `stage01` is per-voxel (it is folded into the merge key), so
// crack WIDTH steps at a voxel boundary where the neighbouring stage differs, even though the
// crack GEOMETRY flows through unbroken. That is intended — it is how a player reads which
// voxel is closest to failing — and the seam test (doc §7) must damage both sides of the tested seam
// to the same stage or it fails for that legitimate reason.

#ifndef CRACK_GLSL
#define CRACK_GLSL

// Fracture cell size, in world units, for the PRIMARY network: the SUBCUBE lattice (1/3 m).
//
// MEASURED, not chosen by taste. The first P3 build put this on the microcube lattice (1/9 m)
// to align with V1.5's spall chips (doc §9). It reads beautifully at 4 units and is INVISIBLE in
// play: a 1 m face carries a 9x9 network, which at 16 units is ~5 px per cell with a crack
// width a fraction of that -- sub-pixel. Measured at 16 units, that build's stage steps were
// 4.55 / 2.08 / 2.02 luminance against a within-stage noise floor of ~1.6-3.2, i.e. the upper
// steps were INSIDE THE NOISE and it was LESS legible than the flat darkening it replaced.
//
// 1/3 m gives a 3x3 primary network per face (~15 px per cell at 16 units), which survives
// minification. The spall-alignment argument is preserved by the SECOND OCTAVE below, which
// lands on the microcube lattice -- so chips still fall where fine cracks already are.
//
// This is the resolution of the legibility risk the review rig flagged in advance ("the rig is optimistic
// about visibility"). The rig was optimistic; the first cell size was the thing it hid.
const float kCrackCell = 1.0 / 3.0;

// Forward declaration: worldFaceUV is DEFINED IN voxel.frag below this include, and GLSL
// requires declaration before use. Declaring it rather than duplicating the projection is
// deliberate: the crack lattice and the tile-rotation lattice must agree on what a world
// cell is, and two copies of that projection would be free to drift apart.
vec2 worldFaceUV(vec3 wp, vec3 n);

// 2D hash -> [0,1)². Same shape as voxel.frag's hash21 but vector-valued, for Voronoi sites.
vec2 crackHash22(vec2 p) {
    vec3 q = vec3(dot(p, vec2(127.1, 311.7)),
                  dot(p, vec2(269.5, 183.3)),
                  dot(p, vec2(419.2, 371.9)));
    return fract(sin(q.xy) * 43758.5453);
}

// Cellular (Voronoi) EDGE distance: F2 - F1 over a 3×3 neighbourhood of cells.
//
// F2-F1 is ~0 exactly on the boundary between two Voronoi cells and grows toward cell
// interiors, so small values trace a connected network of cell walls — a fracture network,
// not a scatter of blobs. This is why cracks join up instead of looking like noise.
float crackEdgeDistance(vec2 p) {
    vec2 cell = floor(p);
    vec2 f    = p - cell;

    float f1 = 8.0;   // nearest site distance
    float f2 = 8.0;   // second nearest
    for (int j = -1; j <= 1; ++j) {
        for (int i = -1; i <= 1; ++i) {
            vec2 g = vec2(float(i), float(j));
            // Jitter each site off its cell centre so the network is irregular rather than a
            // visible square lattice. 0.5 amplitude keeps every site inside its own cell,
            // which is what lets a 3×3 search be exact.
            vec2 site = g + 0.5 + (crackHash22(cell + g) - 0.5);
            float d = length(site - f);
            if (d < f1) { f2 = f1; f1 = d; }
            else if (d < f2) { f2 = d; }
        }
    }
    return f2 - f1;
}

/**
 * The crack field.
 *
 * @param worldPosAbs absolute world position (NOT camera-relative — voxel.frag reconstructs it
 *                    as vChunkBaseAbs + (inWorldPos - vChunkBaseRel), the same exact,
 *                    camera-independent seed the `varied` tile hash uses)
 * @param faceNormal  face normal, to project onto the face plane
 * @param stage01     damage stage normalized to [0,1]: 0 = pristine, 1 = at break
 * @param style       fracture character, from the material (doc §4):
 *                      style < 1  → dense fine network   (brittle: Glass)
 *                      style ~ 1  → medium               (Stone)
 *                      style > 1  → sparse wide fissures (ductile: Steel)
 * @return crack mask in [0,1]: 0 = intact surface, 1 = fully open crack.
 */
float crackField(vec3 worldPosAbs, vec3 faceNormal, float stage01, float style) {
    if (stage01 <= 0.0) return 0.0;

    // Project onto the face plane. worldFaceUV is voxel.frag's own helper, reused rather than
    // duplicated so the crack lattice and the `varied` tile lattice agree on what a world cell is.
    vec2 p = worldFaceUV(worldPosAbs, faceNormal) / (kCrackCell * max(style, 0.15));

    // Stage WIDENS the field; it does not swap it (doc §4). Because width is continuous in
    // stage, a voxel advancing 1→2→3 shows THE SAME CRACKS GROWING rather than three unrelated
    // patterns — the thing stamped decals cannot do.
    float e = crackEdgeDistance(p);
    float width = mix(0.012, 0.075, stage01);          // half-width of the primary network
    float crack = 1.0 - smoothstep(0.0, width, e);

    // Second octave admitted from stage ~0.5: finer branching off the primary walls, so the
    // top stages read as a shattering network rather than a wider version of one line.
    //
    // x3.0 exactly, so this octave sits on the MICROCUBE lattice (1/3 / 3 = 1/9 m) while the
    // primary sits on the subcube lattice. That keeps the spall promise (doc §9) -- V1.5's spall chips are
    // microcube-resolution and will fall where these fine cracks already are -- while leaving
    // the legible structure at a scale that survives distance. Detail up close, readability far.
    float branch = smoothstep(0.45, 1.0, stage01);
    if (branch > 0.0) {
        float e2 = crackEdgeDistance(p * 3.0 + vec2(13.7, 7.3));
        float c2 = 1.0 - smoothstep(0.0, width * 0.6, e2);
        // Branches only exist ON the damaged material, and are strongest near primary cracks.
        crack = max(crack, c2 * branch * 0.85);
    }

    return clamp(crack, 0.0, 1.0);
}

#endif // CRACK_GLSL
