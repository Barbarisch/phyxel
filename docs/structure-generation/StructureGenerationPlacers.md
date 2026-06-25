# Structure Generation — Placers & Quality Checklist

> The architecture for grounded, rich, believable, functional structures: a pipeline of small,
> single-purpose **placers** (one algorithm, one job), and the granular **checklist** every generated
> structure must satisfy. Companion to `docs/structure-generation/StructureGenerationV2.md` (overall design) and
> `docs/structure-generation/StructureBrief.md` (the intake). Standing rules apply: every dimension is grounded/cited (no
> placeholders, no faking), and the **engine** does the ground-truth generation — not hand-authoring.
>
> Status legend: **[D]** done · **[P]** partial · **[M]** missing. (Today only `FurniturePlacer` is a
> real standalone placer; everything else is fused in the monolithic `StructureRealizer::realizeShell`
> and must be split out.)

## Part 1 — The placer pipeline (single-function builders, ground-up)

Each placer takes the brief + prior layers + grounded canons, emits voxels/objects, and is independently
testable. They run in order; each is gated by the relevant checklist items in Part 2.

| # | Placer | Job | Status |
|---|--------|-----|--------|
| 1 | `analyze_site` | sample terrain under footprint → grade, slope, obstructions, water, approach direction | **P** (median seat only) |
| 2 | `prepare_pad` | cut high / fill low to a level build pad out of bumpy terrain; retain/terrace edges | **M** |
| 3 | `place_foundation` | footings to bearing (stepped on slope) + foundation/plinth walls + slab/crawlspace/basement | **P** (crawlspace ring; `basement` = 3-cube void stub — no floor/rooms/access) |
| 4 | `place_subfloor` + `place_floor` | structural floor + finish floor per room (material by status) | **P** (one slab) |
| 5 | `generate_room_layout` | derive room rects from typology + bay model + program (replace hand-authoring) | **M** |
| 6 | `place_exterior_walls` | perimeter walls, style thickness, exterior grade | **P** (fused) |
| 7 | `place_interior_walls` | partitions on shared boundaries, thinner | **P** (fused) |
| 8 | `cut_openings` | carve door/window/arch + sills + reveals + lintels | **P** (gaps only) |
| 9 | `place_doors` | door leaves, correct handedness/swing, register with `DoorManager` | **M** |
| 10 | `place_windows` | glazing / shutters / boards / open per period+status | **M** |
| 11 | `place_ceiling` / `place_intermediate_floor` | ceiling or upper-story floor; defer stairwell holes | **P** (fused; single story — no upper-story floor built) |
| 12 | `place_stairs` | stairs between stories / to cellar, with landings + headroom | **M** (realizer never reads `ProgStair`) |
| 13 | `place_roof` | gable/hip/valley over the real outline + eaves/fascia/soffit + ridge + attic void | **P** (gable on rect, blocky, floats; attic void sealed & inaccessible) |
| 14 | `place_chimney` | flue from each hearth up through the roof | **M** |
| 15 | `place_trim` | baseboards, casings, quoins, exposed framing, string courses (micro detail) | **M** |
| 16 | `place_furniture` | furniture by purpose; wall/center; facing; clearance; on-floor | **D** (`FurniturePlacer`) |
| 17 | `place_fixtures` | function fixtures (altar, forge, bar, loom, hearth) | **P** (via furniture map) |
| 18 | `place_lights` | lamp props + point lights per room; daylight accounting | **M** |
| 19 | `place_clutter` | grabbable surface props on tables/shelves/mantels | **M** |
| 20 | `place_entry` | steps / threshold / path from grade to the door | **P** (step logic) |
| 21 | `zone_parcel` | lay out the lot: front yard (approach/public) vs back yard (private/utility), setbacks | **M** |
| 22 | `place_fence` | fence/hedge along the lot boundary; posts at grounded spacing; gate where the path crosses | **M** |
| 23 | `place_boundary_wall` | freestanding garden/retaining/dry-stone wall between points | **M** (v1 `generateWallSegment` primitive only) |
| 24 | `place_path` | graded path door → road; surface material; steps on slope; routes around beds | **M** |
| 25 | `place_garden` | beds, hedges, herb/kitchen garden, ornamental planting, trees/shrubs in the yard | **M** (distinct from world-gen flora) |
| 26 | `place_farm` | fields/crop rows + furrows, pasture, fallow; sized to the plot | **M** |
| 27 | `place_outbuildings` | barn, byre, sty, dovecote, well-house, privy, smithy — sited relative to the dwelling | **M** |
| 28 | `place_livestock_pens` | pens/folds/runs adjacent to the farmstead; muck away from dwelling/water | **M** |
| 29 | `place_yard_props` | well, trough, woodpile, cart, beehive, midden — on the ground, sensibly sited | **M** |
| 30 | `register_systems` | doors→DoorManager, occupancy (CPU+GPU), navgrid, location markers | **P** |
| 31 | `place_fortifications` | *(fortified only; runs with the structural placers)* curtain wall + flanking towers + gatehouse + parapet/crenellations + moat/ditch | **M** |
| 32 | `place_graveyard` | *(religious only; parcel tier)* consecrated plot, oriented graves, markers by status, lych-gate, charnel/crypt | **M** |
| 33 | `apply_seasonal_state` | *(final dressing pass)* snow / foliage / crops / smoke / shutters / ice by season + time | **M** |
| 34 | `excavate_basement` | *(vertical; see Part 5)* dig the below-grade box into terrain; spoil removal; drainage; window wells / bulkhead | **M** |
| 35 | `place_basement` | occupiable below-grade story at level −1: retaining walls (stone, thick) + base slab + cellar floor/ceiling; runs the per-story placers | **M** |
| 36 | `stack_stories` | the multi-story loop — run the per-story placers at each story's base-Y, then realize each `ProgStair`; prerequisite for upper floors, basements & attics | **M** (missing — realizer hard-codes `stories[0]`) |
| 37 | `place_attic` | story inside the roof volume: usable-area mask from pitch (headroom ≥ 1.5 m), knee walls, sloped ceiling, dormers/gable lights, hatch/stair access | **M** |
| 38 | `site_settlement` | *(settlement tier; see Part 7)* justify the location (water / defence / crossroads / harbour / resource); set growth seed + axis | **M** |
| 39 | `lay_street_network` | road hierarchy (high street → lane → alley), organic vs planned, market at the crossing; widths from canon | **M** |
| 40 | `subdivide_plots` | burgage plots along street frontage (narrow × deep), fractional-perch subdivision, back lanes | **M** |
| 41 | `zone_districts` | districts by wealth + trade; noxious trades to the edge / downwind / downstream; mixed-use core | **M** |
| 42 | `place_town_wall` | wall circuit + towers + gates (reuses #31); intramural vs extramural growth; ditch | **M** |
| 43 | `place_public_spaces` | market square, well / conduit / fountain, green, gallows / pillory, churchyard | **M** |
| 44 | `place_bridges` | crossings over river / ditch; bridge houses | **M** |
| 45 | `populate_plots` | per plot, pick a Part 6 archetype by district + status + frontage and run the building pipeline (#1–37) | **M** |
| 46 | `compose_compound` | realize compound archetypes (castle / monastery / cathedral) as a walled mini-settlement of sub-buildings | **M** |
| 47 | `place_signage` | pictorial trade / inn signs at shopfronts; wayfinding (illiterate clientele) | **M** |
| 48 | `dress_street_life` | street props of occupation: market stalls, woodpiles, laundry, refuse, carts, animals | **M** |
| 49 | `link_subterranean` | stub sewer / cellar / crypt connections for the subterranean tier (handoff) | **M** (deferred tier) |
| 50 | `excavate_subterrane` | *(subterranean tier; see Part 8)* carve the below-grade void into chunk terrain + remove/backfill earth — the core engine gap | **M** (engine cap missing) |
| 51 | `carve_sewer_network` | vaulted drains under the streets, fed by garderobe chutes + cesspits, gravity to a river outfall, surface gratings | **M** |
| 52 | `place_crypt` / `place_catacomb` | burial chambers + loculi niches under churches / cemeteries; multi-level galleries | **M** |
| 53 | `excavate_dungeon` | cells, oubliettes, corridors, chambers under a castle / keep — the adventure-site layer | **M** |
| 54 | `place_mine` | adit → gallery → shaft following an ore seam; timber supports; spoil at the mouth | **M** |
| 55 | `connect_underground` | link cellars ↔ tunnels ↔ sewers ↔ crypts ↔ dungeon into a navigable graph (multi-level connectivity gap) | **M** (engine cap missing) |
| 56 | `place_secret_passages` | hidden doors / tunnels between buildings or to escape routes; mechanisms | **M** |
| 57 | `validate_crawlability` | prove the network is traversable: widths, headroom, reachability, encounter / light spacing — the playability gate | **M** |
| 58 | `author_world_bible` | *(fantasy tier; see Part 9)* author / load + consistency-validate + persist the setting canon (magic, pantheon, races, factions, era); per-world | **M** |
| 59 | `apply_setting_overlay` | overlay the bible onto grounded archetypes: arcane programs, deity iconography, race material palettes, magical lighting; flag bible-licensed impossibilities as engine gaps | **M** |

## Part 2 — The granular checklist

Every check below is a gate. Many are **conditional on the brief** (period, region, culture, climate,
function, owner status, condition). A check that can't be evaluated because data is missing → STOP and
add the data, never assume.

### A. Setting & period coherence
A1. Construction method exists in the declared period+region (no modern stud wall in a medieval cottage).
A2. Materials available in the period+region (no Portland cement, no float glass before its era).
A3. Room program fits the period (no "home office" / "garage" in a croft).
A4. Furniture *types* are period-correct (trestle table, chest, settle — not a flat-pack desk).
A5. Architectural vocabulary matches the culture/region (Norse stave vs Mediterranean courtyard vs Tudor frame).
A6. Technology limits respected (window pane size, beam span, arch span, dome feasibility by era).
A7. Glazing tech gated by period+status (open → shutter → oiled cloth → leaded quarries → glass).
A8. Magic only where the lore allows it (no floating masonry unless enchantment is canon).
A9. No anachronisms — no feature that postdates the declared period.
A10. Lore/faction fit (a temple's deity iconography; a guild hall's trade; a watchtower's allegiance).
A11. Decorative motifs match the culture (knotwork vs acanthus vs geometric).

### B. Climate & environment response
B1. Roof pitch suits the climate (steep to shed heavy rain/snow; shallow only where dry).
B2. Wall mass/insulation suits temperature (thick cob/stone for cold; thin/airy for hot).
B3. Window size/orientation suits climate (small & few in cold; large/shaded in hot).
B4. Eaves/overhangs sized to throw water clear of the walls.
B5. Orientation uses sun/prevailing wind where it matters (hearth wall to windward, etc.).
B6. Flood/drainage sense — not seated in a sink; raised where wet.
B7. Snow load reflected in roof structure if a snow climate.

### C. Social status, wealth & quality tier
C1. Overall size scales with status (croft ≪ yeoman ≪ manor ≪ keep).
C2. Finish quality scales (rough vernacular → dressed → fine ashlar/carved).
C3. Material grade scales (daub vs stone; thatch vs slate vs lead; earth floor vs flagstone vs tile).
C4. Window count/size/glazing scales with wealth (glass = rich/late).
C5. Ornamentation scales (quoins, moldings, carving, heraldry only for wealth).
C6. Room count & specialization scale (one room vs hall+solar+service+kitchen+chapel).
C7. Furniture quantity & quality scale with status.
C8. Ceiling height scales (humble low; grand tall — "higher than wide" for a great hall).
C9. Defensive features only where status/function warrants (a manor isn't a castle).

### D. Structural integrity (engineering reality)
D1. **No floating voxels** — every solid connects to ground or structure (no orphans).
D2. Foundation reaches bearing under every column; stepped on slopes (no gap beneath).
D3. Load path closes: floor on foundation, walls on floor/foundation, roof on walls.
D4. No wall starts mid-air; walls are continuous and plumb.
D5. Every opening has a lintel/arch carrying the wall above it.
D6. Spans within material limits (timber beam ≤ ~tree length; masonry arch/dome feasible).
D7. Roof structure is self-supporting (ridge + rafters/purlins implied; gable ends closed).
D8. Corners tied (quoins / interlocking) — walls don't just abut.
D9. Cantilevers/jetties within reason (jetty overhang ≤ joist limit).
D10. Wall height-to-thickness within a stable ratio (a thin wall can't be very tall).
D11. Chimney/flue is continuous and supported, penetrates the roof.
D12. Floors don't sag-span beyond joist limits (intermediate support for wide rooms).

### E. Grounded dimensions (the canon — cited)
E1. Wall thickness = the style/period assembly value (cited; exterior ≠ interior).
E2. Story/ceiling clear height ≥ character clearance and ≥ period/code minimum.
E3. Each room's floor area within its typology's grounded range (not too small/large).
E4. Footprint width ≤ the frame/cruck span; length:width within typology proportion bounds.
E5. Bay length consistent across the building.
E6. Door clear width/height fit the character + the chosen leaf.
E7. Window opening size + sill height grounded (not floor-level).
E8. Stair rise/run/going + nosing within ergonomic/code bounds; consistent per flight.
E9. Corridor/passage clear width ≥ the minimum to pass.
E10. Threshold/step rise ≤ the maximum comfortable rise.
E11. Hearth/fireplace opening + hearth depth grounded.
E12. Furniture footprints from the DimensionCanon (cited).
E13. Counter/work-surface, table, seat, bed heights body-derived (cited).

### F. Room program & layout logic
F1. Room set appropriate to function + status + period.
F2. Sensible adjacencies (kitchen near hall; bedrooms grouped; privy away from kitchen/food).
F3. Hearth/kitchen against an exterior wall (chimney can vent).
F4. Wet/service rooms (bath, privy, scullery, byre) grouped and peripheral.
F5. The main room (hall) is central and/or the largest.
F6. No room is a pure corridor unless intended; no leftover slivers.
F7. Room aspect ratio reasonable (no 1×10 rooms).
F8. Storage/service present where the function needs it (cellar, pantry, byre, undercroft).
F9. Vertical stacking sensible (heavy/wet/service low; private/light high).

### G. Circulation, access & function
G1. Exactly the intended number of exterior entrances; a clear main door.
G2. Every room reachable from the entrance through passable openings + stairs.
G3. Character physically fits through every door/passage (clear w/h).
G4. A walkable path exists across each room (furniture leaves clearance).
G5. No furniture/fixture on a doorway threshold or its swing.
G6. Stairs reach every story; landings have headroom and turning space.
G7. Doors actually open — registered, with swing clearance, correct handedness.
G8. Threshold flush or stepped to exterior grade so a character can walk in.
G9. Zoning/privacy gradient (public near entry, private deep; don't route through a bedroom to the kitchen).
G10. Habitable rooms have egress (a window or second exit) where the scene needs it.
G11. No unreachable dead space (unless a deliberate hidden room).

### H. Openings — doors & windows
H1. Door openings framed (jambs + lintel + threshold), not raw gaps.
H2. The main/front door is distinguished (grander, central, on the approach side).
H3. Doors hung on the correct side so the leaf swings into the clear/appropriate space (handedness).
H4. Windows on exterior walls only (interior windows only if intended).
H5. Window count/size/spacing/glazing per period + status + climate.
H6. Windows sit on a sill at a sensible height (not floor-level holes).
H7. Openings kept off the corners (structural; quoins intact).
H8. Window/door rhythm reads deliberately (aligned, regular for ordered styles).

### I. Roof & weatherproofing
I1. Roof covers the **entire** footprint, including lower wings (no open-topped room).
I2. Roof form suits the plan shape + region (gable / hip / valley / conical thatch).
I3. Pitch matches the roof material (thatch ≥45–50°, clay tile 35–45°, slate ≥25°…).
I4. Eaves overhang throws water clear of the walls.
I5. Hips/valleys/ridges resolve correctly over articulated outlines.
I6. Gable ends are thin walls, **not** full 1 m cubes.
I7. Attic/roof void present; headroom checked if habitable.
I8. Chimney(s) penetrate the roof above each hearth, flashed.
I9. Roof material period + region + status appropriate.

### J. Materials, finish & condition
J1. All materials period + region + status appropriate (cited).
J2. Exterior vs interior finishes differ appropriately (daub/limewash ext; plaster int).
J3. Floor material per room by status (earth → timber → flagstone → tile).
J4. Trim/relief present (baseboard, casing, quoin, exposed frame) — never bare flat cubes.
J5. No magenta/missing-texture material anywhere.
J6. **No placeholder material masquerading as the real thing** (honesty rule).
J7. Condition consistent across the build (new vs worn vs aged vs ruined applied uniformly).
J8. Weathering logic (moss on the north/damp side; soot above the hearth) if condition warrants.

### K. Furniture, fixtures & lighting
K1. Each room gets purpose-appropriate furniture (bed→bedroom, counter→kitchen, altar→temple).
K2. Furniture **faces into the room** (correct rotation from its wall).
K3. Casegoods back onto a wall (not floating mid-room).
K4. Furniture rests **on the floor** (no float, no sink).
K5. No furniture overlaps a wall or another piece.
K6. No furniture blocks circulation or a door.
K7. Furniture quantity scales with room size + status (not one stool in a great hall).
K8. Every room has light — a window (daylight), hearth, or lamp; no pitch-black room.
K9. A hearth/fireplace in hall/kitchen, aligned with its chimney.
K10. Function fixtures present (forge in a smithy, bar in a tavern, loom in a weaver's, pews+altar in a chapel).
K11. Surface clutter (lived-in) appropriate to function + status; clutter is grabbable, not baked.
K12. Beds/seating oriented sensibly (headboard to wall; seating toward hearth/table).

### L. Site, parcel & context
L1. Seated **flush** on a flattened pad (cut/fill); not perched, not buried.
L2. Foundation/plinth visually integrated with the grade.
L3. Entry steps/path from grade up to the door where the ground is lower.
L4. No overlap with neighbours, existing structures, or terrain features.
L5. Oriented to its approach (entrance faces the road/path/view).
L6. Fits its plot/setbacks (urban party walls vs rural openness).
L7. Fence ring + gate where a parcel is intended; gate aligns with the path.
L8. Yard props (well, garden bed, woodpile) where appropriate; none float or clip terrain.
L9. Path connects the door to the road/network.

### M. Believability & aesthetic coherence
M1. Massing articulated for larger buildings (wings/ells/courtyard — not a flat box).
M2. Symmetry/regularity per culture + status (vernacular organic; noble ordered).
M3. Scale reads correctly beside the 1.751-cube character.
M4. Proportions pleasing (no squat or weirdly-towering masses).
M5. A clear focal point (grand door, chimney stack, tower, gable).
M6. Style is cohesive (don't mix Gothic + Norse + Tudor at random).
M7. Repetition-with-variation across multiple buildings (not identical clones).
M8. Signature features present for the declared style (jetty, crow-steps, exposed framing, dragon posts).

### N. Engine / performance / systems
N1. Voxel count within the per-structure budget.
N2. Resolution used appropriately (subcube bulk, micro for detail — not all-micro).
N3. Static collision/occupancy registered (CPU + GPU) so the character won't fall through.
N4. Navgrid rebuilt over the structure so NPCs can path in/through.
N5. Doors/interactive objects registered (openable, lockable where specified).
N6. Location markers emitted (rooms, entrance) for gameplay/quests.
N7. Deterministic + reproducible from the same brief + seed.

### O. Process & provenance integrity
O1. The StructureBrief is complete and validated **before** any voxel.
O2. Every dimension is cited/grounded; no silent invention.
O3. Each placer's output is validated before the next runs.
O4. Missing capability/material is logged as a gap (gaps docs) — never faked.
O5. The engine performs the generation; nothing is hand-placed to fake a result.

### P. Parcel, landscape & agriculture
P1. Lot is zoned: front = approach/public (path, maybe ornamental); back = private/utility (kitchen garden, woodpile, privy, animals).
P2. A fence/hedge/wall encloses the lot; type + height by purpose + status (stock-proof vs privacy vs decorative vs defensive).
P3. Fence posts at the grounded spacing; the fence follows the boundary and steps with the terrain.
P4. A gate aligns with the path; gate clear width ≥ character (and ≥ cart width for a farm/yard).
P5. A path connects the door to the road/network; graded + stepped on slope; it routes *around* beds, not through them.
P6. Boundary/retaining walls are coursed sensibly, battered/tied where tall, and reach the ground (no float).
P7. Plants are grounded: species suit the climate + region + period (no tropical palms in a cold croft); each is rooted in soil.
P8. No plant floats, sinks, or clips a wall/path/structure; canopy doesn't intersect the roof.
P9. Kitchen/herb garden sits by the kitchen door; ornamental planting fronts the public approach.
P10. Trees set back from the building (root/fire/shade clearance); orchard planted in rows if intended, else organic scatter.
P11. Planting density reads natural (organic scatter), unless a *formal* garden/field (then deliberate geometry).
P12. Farm fields are sized/shaped to the plot; furrows/crop rows aligned and consistent; fallow vs cropped logic if simulated.
P13. Livestock pens/byre adjoin the farmstead; the midden/muck is downwind and away from the dwelling and water.
P14. Outbuildings sited by use: barn near fields, well near the kitchen, dovecote/sty peripheral, smithy clear of thatch (fire).
P15. A water source (well/trough/stream access) is placed and is **not** contaminated by the privy/midden.
P16. Yard props rest on the ground (no float/clip); woodpile near the hearth door; cart by the gate/barn.
P17. The whole parcel is grounded to the terrain (pad/grade), not floating above or buried in bumps.
P18. Distinct from world-gen flora: parcel planting is authored for the lot — do NOT reuse the terrain biome scatter as a garden.

### Q. Defense & fortification *(when the brief sets defensibility > none)*
Q1. Defensive wall thickness/height grounded to the threat era (siege-engine-proof for a keep; lighter for a fortified manor).
Q2. Parapet with crenellations (alternating merlons + crenels) along the fighting top.
Q3. A wall-walk / allure behind the parapet, wide enough to fight and move along.
Q4. Flanking towers at corners and at intervals so every wall face is covered (no dead ground).
Q5. Arrow loops / embrasures sited for coverage, splayed wide on the inside, narrow outside.
Q6. A gatehouse controls the single main entry (gate + portcullis + drawbridge as era/status warrants).
Q7. Murder-holes / machicolations above the gate and other vulnerable points.
Q8. The gate is the weakest point — defenses concentrate there; a bent/L entry prevents a straight charge.
Q9. A talus/batter at the wall base (resists undermining, deflects dropped objects).
Q10. Defenders hold the height — the whole approach is overlooked; a cleared glacis/killing ground outside (no attacker cover).
Q11. Overlapping fields of fire from towers — verify no blind spot along the curtain.
Q12. Layered defense: outer bailey → inner bailey → keep/donjon as the last refuge.
Q13. Moat/ditch/berm where the ground allows, crossed only at the controlled gate.
Q14. Posterns/sally ports are small, few, and defensible.
Q15. A well **inside** the walls + siege storage (granary/cistern) — can withstand a siege.
Q16. Walls exploit defensible terrain (hilltop, cliff, river, marsh) where present.
Q17. Garrison support inside (quarters, armory, guardrooms at the gate).
Q18. Line-of-sight check: from each tower/wall-walk, the approach and adjacent walls are visible.

### R. Religious, ritual & burial *(when function is a temple / church / shrine / monastery)*
R1. Sacred orientation correct for the faith (e.g. a church's altar/chancel to the EAST, main entry to the west; other faiths their own axis — toward a holy direction/sunrise).
R2. Axial/processional plan where the rite needs it (narthex/porch → nave → chancel → altar).
R3. The altar / holy focus is the spatial and visual climax (terminates the axis, emphasized by light).
R4. Holy-of-holies / sanctuary at the deep, restricted end; public space at the entry.
R5. Worship seating/space oriented toward the holy focus (pews face the altar).
R6. Font/stoup at the entrance (baptism/purification) where the faith uses one.
R7. Bell tower / campanile / minaret if the faith calls a congregation.
R8. Light emphasizes the sacred: clerestory/rose/east window over the altar; the nave is lit from above.
R9. Iconography matches the deity/faith (no crosses in a pagan temple; correct symbols).
R10. Sacristy/vestry for vessels and vestments; monastic adds cloister + refectory + dormitory + scriptorium.
R11. A consecrated boundary (wall/hedge/lych-gate) separates sacred ground.
R12. **Graveyard:** consecrated churchyard adjacent; graves oriented per the faith (Christian: E–W, head to the west).
R13. Grave rows/spacing regular; markers (headstone/cross/effigy) scale with status; lych-gate at the churchyard entry.
R14. Crypt/ossuary/charnel for higher-status or space-constrained burial.
R15. Unconsecrated burial (excommunicate/unbaptized/suicide) is OUTSIDE the consecrated wall.
R16. Memorials/tombs (brasses, effigies, chantry) placed by status, not blocking circulation.
R17. A processional approach/path to the main door; the building fronts it.

### S. Season & temporal state *(season + time-of-day are brief parameters)*
S1. Season is read from the brief (spring/summer/autumn/winter) and drives appearance + contents — not assumed.
S2. Snow accumulates on roofs/ground in winter (more on flat/shallow pitch, sheds off steep/thatch); none in summer.
S3. Foliage matches the season: bare trees + dormant beds (winter), blossom (spring), full green (summer), turning/leaf-fall (autumn).
S4. Fields match the season: ploughed/bare (winter), sprouting (spring), standing crop (summer), stubble + hayricks/sheaves (autumn harvest).
S5. Water state: ice on ponds/troughs in deep winter; mud in the thaw; low/dusty in high summer.
S6. Hearths/chimneys show smoke in cold seasons (fires lit); none on a warm day.
S7. Shutters/windows: closed in winter/at night; open in fair weather/day.
S8. Firewood stock scales inversely with the season (high before winter, depleted by spring).
S9. Livestock are in the byre/fold in winter, at pasture in summer.
S10. Garden/orchard state matches the season (dormant → planted → ripe → harvested/bare).
S11. Sun angle + shadows reflect the season + time of day (low long winter sun; high summer noon); lamps lit at dusk.
S12. Weathering/staining consistent with the season (damp/snow-melt streaks, mud at thresholds in wet seasons).
S13. Seasonal/temporary structures only where apt (a shieling/summer hut used only in summer; threshing floor active at harvest).
S14. Festive/seasonal dressing (harvest, midwinter) only if the scene calls for it — not by default.
S15. Snow/foliage/crop state is a **dressing pass over a season-agnostic build** — the structure itself isn't rebuilt per season.

### T. Per-room function tests *(uses the Part 3 programs)*
T1. Every room is identifiable as its declared purpose by its REQUIRED fixtures — a "kitchen" with no cooking station, or a "bedroom" with no bed, **FAILS the function test**.
T2. Required fixtures are present and grounded; typical fixtures are added and scale with status (a lord's bedchamber gets more than a servant's).
T3. Each function-defining fixture is correctly **serviced**: a hearth/oven is vented (chimney), a forge has a quench trough + is clear of thatch, a privy has a chute to a pit, a loom/scriptorium has window light, a stable has feed+water+muck-out.
T4. Required contents are **period-correct** (medieval bedchamber = bed + chest, NOT wardrobe/dresser/closet; a "closet" is a small private chamber).
T5. No required fixture floats, blocks a door, or duplicates pointlessly; counts scale with room size.

### U. Content library & the long tail
U1. Assets are requested from the library by **(category + tags: size/status/style/period)** — and only **approved** assets are used.
U2. A missing asset is handled by a grounded **same-category substitute (logged as a downgrade)**, or — for function-critical items — a **flagged marker**. NEVER a silent fake, never a placeholder wearing the real thing's name.
U3. A newly generated asset is validated + grounded + **user-approved** + **persisted** (template + dims + provenance) before any reuse; once approved it is a permanent library item.
U4. The library is the **engine's data** (committed `resources/` + the per-world DB), loaded at runtime — reproducible **without** Claude. Claude at most *drives* the authoring pipeline; it is never the memory.
U5. Every gap (missing asset / material / category) is appended to a **"wanted" backlog** so it is authored once and never re-improvised.
U6. Wall art (paintings, tapestries, reliefs) needs a **decal / framed-picture mechanism** — a current gap: flag it, don't fake it.

### V. Vertical & multi-level integrity *(basements, upper floors, attics — see Part 5)*
V1. Every story (basement / ground / upper / attic) is **reachable** by a *built* stair, ladder, or hatch — `place_stairs` actually realizes each `ProgStair`. No sealed, inaccessible levels (today's basement void + roof void both fail this).
V2. Stair geometry meets the comfort floor — riser ≤ 0.196 m, tread ≥ 0.254 m, width ≥ 0.914 m, headroom ≥ 2.032 m (IRC R311.7) — OR a *grounded* period stair (medieval newel/ladder stairs run steeper+narrower). Never an un-climbable or floating flight.
V3. Floors stack at the correct base-Y with no gap/overlap; an intermediate floor IS the ceiling of the story below; stairwell holes are cut through **both** the floor and the ceiling slab.
V4. A basement is **occupiable, not a void**: retaining walls hold back earth (stone, ≥ the masonry exterior thickness), a base slab, headroom ≥ 2.032 m (storage) / 2.134 m (habitable) per IRC R305, excavated into terrain (not perched), with an access.
V5. Basement light/air per use: storage may be windowless + vented; habitable below-grade needs an egress well (opening ≥ 0.530 m², sill ≤ 1.118 m, well ≥ 0.84 m² — IRC R310) — *conditional on period* (medieval cellars used light vents, not code egress).
V6. Below-grade implies **damp control** — drainage + siting in well-drained ground; flagged, never ignored.
V7. Attic **usable floor = only where headroom ≥ 1.5 m** under the pitch; the attic room rect is the masked area, NOT the full footprint; knee walls close the unusable eaves.
V8. Attic has **access** (stair/ladder/hatch) and **light** (gable window or dormer) — not sealed and dark.
V9. Roof structure suits an occupiable attic (hollow shell + collar height) vs a solid wedge; rafters/ridge don't intrude below standing headroom in the usable zone.
V10. **Vertical loads stack** — upper-story and roof walls bear on walls/posts below, not mid-span on a floor (the D-category structural rule, applied in Y).

### W. Archetype identity & fidelity *(uses the Part 6 library)*
W1. The built structure **reads as its declared archetype** — silhouette / massing / signature features identify it without a label (a tavern isn't a generic box; a keep isn't a tall cottage).
W2. The archetype's **signature features are present** (tower's first-floor entrance + parapet; church's oriented chancel + tower; shop's street counter + pictorial sign; smithy's forge + chimney).
W3. The archetype's building-level **function_test passes** — it composes the Part 3 per-room tests for every required room.
W4. Form **scales with status + period + region** (rich townhouse vs slum tenement; Norman square keep vs later round keep).
W5. **Compound archetypes** (castle, monastery, cathedral) correctly compose sub-buildings + walls + parcel — not a single mega-room.
W6. **Mixed-use** is honored where period-correct (shop/workshop ground floor + dwelling above in a townhouse).
W7. Every archetype dimension is **grounded or flagged `to_ground`** — no invented footprints/heights (the standing rule, at building scale).

### X. Settlement siting & street network *(settlement tier — see Part 7)*
X1. The settlement's **location is justified** — water, defensibility, a crossroads, a harbour, a resource — not placed arbitrarily.
X2. A **street hierarchy** exists (high street → lane → alley → court) with grounded widths; the high street links the gate(s) to the market.
X3. The network is **connected** — every plot reaches the street network reaches a gate; no orphaned blocks.
X4. **Form matches origin** — organic (accretive, curving around a feature) vs planned (grid / bastide) per the brief.
X5. Gates + bridges sit on the through-routes and are **passable** (a cart can actually get through).

### Y. Plots, blocks & density
Y1. City buildings sit on **burgage-style plots** (narrow frontage × deep), street-fronting, **party walls** in the core — NOT freestanding cottages-with-yards inside a city.
Y2. Plot frontage/depth come from the canon; subdivision in **fractional-perch** units.
Y3. **Density gradient** — dense core (high coverage, shared walls) → looser edge → extramural suburb.
Y4. Plot coverage + structures-per-acre within grounded bounds; **back-plots** (gardens / yards / privies / wells) sit behind the street range.
Y5. **Accretion reads** — mixed building ages, infill, encroachment; not a uniform single-build town.

### Z. Districts, zoning & social fabric
Z1. Districts have **distinct character** — the rich quarter ≠ the slums ≠ the craft streets.
Z2. **Trades cluster** (smiths together, the shambles for butchers) and name their streets accordingly.
Z3. **Noxious trades** (tanners, dyers, butchers, slaughter) sit at the edge, **downwind + downstream** of dwellings and the water intake.
Z4. The **wealth gradient is spatial** (near the centre / castle / minster = high; periphery / marsh / against the wall = low) and drives the archetype + status tier per plot.
Z5. **Mixed-use** is the core norm — shop/workshop ground floor + dwelling above.

### AA. Public realm & settlement function testers *(the "does the city function" gate)*
AA1. **Water** — a public source per N people (wells / conduits / fountains / river access) reachable from every district.
AA2. **Food** — markets + bakeries/butchers + granaries/storehouses + surrounding farms feed the population.
AA3. **Waste** — sanitation handled (cesspits / sewers / middens / drainage); not flagged-and-ignored.
AA4. **Defense** — walls / gates / watch sized to the settlement, when the brief sets a threat.
AA5. **Governance** — a seat (town / moot hall, or the lord's castle) + gaol + market authority.
AA6. **Worship** — a church/temple per faith, sited prominently.
AA7. **Trade** — a market + the shop spread a town actually needs (a believable archetype mix, not one of everything).
AA8. **Production** — mills, smiths, workshops scaled to feed + equip the population.
AA9. **Circulation** — every district reachable; main streets, gates, bridges sized for carts; the market accessible.
AA10. A **public square / market** exists as the social heart, at the street crossing.

### BB. Subterranean & dungeon layer *(subterranean tier — see Part 8)*
BB1. Below-grade volumes are **actually excavated** into terrain (void carved, earth removed) — not a box perched in solid ground (the Part 5 basement-stub bug, at network scale).
BB2. Passages are **crawlable/walkable to their intent** — walk-upright galleries ≥ ~2.0 m headroom × ≥ ~0.9 m wide; crawl tunnels honestly narrow (~0.6–1.0 m) and *labelled* as such; no zero-clearance dead solids.
BB3. The network is a **connected graph** — every chamber reachable from an entrance via stairs/ladders/shafts; vertical links between levels exist; no orphaned voids.
BB4. **Sewers follow gravity** to an outfall (river/moat), sit under the streets, take garderobe chutes + cesspits, and have surface gratings/access — not a free-floating maze.
BB5. **Burial spaces** are period/faith-correct — crypts under the chancel, catacomb loculi sized for a body, charnel/ossuary for bones, oriented per rite.
BB6. **Dungeons/cells** are grim + secure (locked/barred, oubliette access from above, no easy egress) and **fit the structure above** (under the keep, not floating).
BB7. **Mines** follow a seam (adit → gallery → shaft), are timbered, and dump spoil at the mouth — not random caverns.
BB8. **Playability** — the layer is a usable adventure site: reachable, lit-or-intentionally-dark, sensible encounter/loot spacing, discoverable secret doors, purposeful dead-ends.
BB9. **Anachronism honesty** — a walkable sewer/dungeon under a medieval brief is *flagged* as a Roman/Victorian/game conceit (allowed for D&D, never claimed as historical).

### CC. Fantasy & setting-canon integrity *(fantasy tier — see Part 9)*
CC1. Every fantasy value **traces to a world-bible entry** — source-or-stop still holds; for fantasy the *source is the bible*, not real history.
CC2. **Internal consistency** — no value contradicts another bible rule (elves who "grow living wood" can't own a cut-ashlar manor); the consistency validator passes.
CC3. The **structural skeleton stays grounded** — a fantasy building is a real archetype (Parts 5–8) + an overlay, not physics-defying — *unless* the bible explicitly licenses it (then it's an engine-gap flag, not a fake).
CC4. **Race/culture architecture** matches the bible — material palette + form + construction logic (dwarven carved-stone halls, elven living-wood, halfling earth-bermed smials).
CC5. **Pantheon iconography** is correct per deity — symbols, sacred colours, orientation, geometry — in temples/shrines.
CC6. **Magical materials** behave per the bible and **map to real engine materials** (magical light → the existing `glow` emissive material; arcane stone → a defined material entry), not invented-on-the-fly visuals.
CC7. **Genre honesty** — what is grounded-real vs bible-fantasy is clearly marked (the Parts 5–8 "anachronism/convention" discipline, applied to magic).
CC8. **Engine-gap honesty** — impossible geometry the bible asks for (non-Euclidean interiors, floating mass, bigger-on-the-inside) is flagged as unsupported, never faked with a hack.

## Part 3 — Per-room function programs (the "what makes it that room" library)

Data-driven recipes the `FurniturePlacer` reads and the **T**-checks enforce. **Required** = without it the room
fails its function test. **Typical** = adds believability, scales with status. **Service** = the functional
hookup that must also be satisfied. Medieval-grounded; add room types as needed.

**Dwelling**
| Room | Required | Typical | Service / functional |
|------|----------|---------|----------------------|
| hall / living | hearth/fireplace | trestle table, benches/settle, stools, chest, wall light | hearth vented (chimney/louvre); seating faces hearth/table |
| kitchen | a cooking station (cook-hearth or oven) | work table / dresser-board, shelving, barrels/crocks, pot-rack, wash basin | cooking station vented; work surface beside it; water access; fire-safe |
| bedchamber | a bed | **chest** (storage), stool, candle/rushlight, chamber pot, washstand (basin+ewer) | bed clear of door swing; room to pass; a light source; door for privacy |
| solar (lord's) | bed + fireplace | seating, window seat, chest/coffer, writing desk (wealthy), wall hangings | private (deep in plan), heated, well-lit |
| privy / garderobe | seat over a chute/cesspit | small vent/window | chute/drain to pit/moat; ventilated; AWAY from kitchen/food/well |
| bathing | a tub | water source, brazier, stool | water in, drainage out, heat |
| pantry / buttery / larder | shelving/storage | barrels (drink), bins/crocks (dry), cold slab (meat) | cool/dark; near kitchen/hall |
| cellar / undercroft | barrel/crate storage | vaulting, racks | cool; below grade; stair/trapdoor access |
| servants' quarters | pallet/simple bed | a chest, a stool | a place to sleep |

**Trade & work**
| Room | Required | Typical | Service / functional |
|------|----------|---------|----------------------|
| smithy / forge | forge/hearth + anvil | bellows, quench trough, tool rack, workbench, fuel | forge vented; anvil by forge; water to quench; **clear of thatch (fire)** |
| workshop | workbench + tools | storage, stool | window light, tool storage |
| weaver's | a loom | spinning wheel/distaff, wool/cloth store | good light |
| bakehouse | an oven | kneading table, flour bin, peel, fuel | oven vented; flour dry; clear of thatch |
| brewery | vats/coppers | hearth, casks, water | water + heat; cask storage |
| mill | millstones + drive (water/wind) | hopper, meal bins, sacks, hoist | power source; grain in / meal out |
| tavern common room | a bar/serving counter | tables, benches/stools, hearth, casks | bar + seating + hearth; route to kitchen/cellar |
| shop / storefront | a counter | display shelving, goods, street shutters, back store | counter to the street; lockable store behind |

**Agriculture & outbuildings**
| Room | Required | Typical | Service / functional |
|------|----------|---------|----------------------|
| byre / cowshed | stalls | hayrack, feed/water trough, muck channel | stalls sized to beasts; feed+water; muck-out; adjoins dwelling/yard |
| stable | stalls | hayrack, troughs, tack rack | stall size; hay/feed/water; muck-out |
| barn | open storage volume | threshing floor, hay mow, cart bay, big doors | cart-width doors; dry; near fields |
| granary | raised/dry storage | bins/sacks, staddle stones | dry; rodent-proofed |
| dovecote / pigsty / coop | nest boxes / sty + trough | — | animals in/out; muck away from dwelling/water |

**Religious**
| Room | Required | Typical | Service / functional |
|------|----------|---------|----------------------|
| nave | congregation space/pews facing the altar | — | oriented to the chancel/altar |
| chancel / sanctuary | an altar | candles, lectern, sedilia, piscina | at the holy (east) end; lit/emphasized; raised/railed |
| sacristy / vestry | aumbry/chest for vessels & vestments | — | adjacent to chancel; secure |
| refectory | long tables + benches | reading pulpit | seating for the community |
| dormitory | beds/cells | — | one per monk; near the church (night stair) |
| scriptorium | desks/lecterns | book storage | strong window light |

**Defensive**
| Room | Required | Typical | Service / functional |
|------|----------|---------|----------------------|
| great hall (keep) | hearth(s) + high table + benches | dais, screens passage, wall hangings | heated; lord's high table at the dais; service end behind the screen |
| guardroom | weapon storage + seating | brazier, table | by the gate; covers the entry |
| armory | weapon/armor racks | — | secure, dry |
| cell / dungeon | a barred/locked door | manacles, straw | secure (no easy egress) |
| gatehouse chamber | portcullis winch / murder-hole access | — | controls the gate |

## Part 4 — Content library & the long tail (how the engine remembers)

The content is effectively infinite — a *Curse of Strahd* mansion alone wants banquet tables, chandeliers,
oil paintings, marble busts, ornate banisters, tapestries, heraldry. We cannot pre-author all of it, and we
**must not** rely on a Claude session to recall or improvise it (it won't, reliably). The answer is a
**persistent, growing, validated library owned by the engine** — and that library, not Claude, is the memory.

**The library = the engine's committed data + the per-world DB (loaded at runtime):**
- `resources/templates/*.voxel` + `template_catalog.json` — voxel assets (furniture, fixtures, sculptures, banisters, chandeliers).
- `resources/materials.json` + textures — materials; wall art as decals/pictures *(see gap below)*.
- `resources/object_dimensions.json` — grounded dims per archetype (`DimensionCanon`).
- the **`AssetLibrary`** (status / version / provenance) — which assets are **approved** for use.
- `resources/room_program.json` + the Part 3 programs — what each room requires, by category.
- `resources/structure_styles.json` — per-style/material params.

All version-controlled; placed instances persist in the world DB. **A session with no Claude has the full
accumulated library.** My role is at most to *drive* the authoring pipeline — never to be the memory.

**Asset taxonomy** (the placers/room-programs request by category + tags): furniture (seating / table /
storage / bed), lighting (candle / sconce / **chandelier** / lantern), wall art (**painting** / tapestry /
relief), free-standing art (statue / bust), architectural detail (**banister** / baluster / molding / column /
quoin), function fixture (altar / forge / bar / loom), clutter (tableware / books / bottles), exterior (fence /
gate / well / plant). Each asset carries tags: **size, status, style, period**.

**"We don't have X" → generate once, persist forever:**
1. **Detect** the gap — no approved library asset matches `(category, tags)`.
2. **Generate** — the asset-gen loop (variants → rank → repair → deterministic + visual gates → **your one-time approval**) writes it to the library with provenance. Templates via the BlockSmith text→voxel pipeline; art via the texture/decal pipeline; dims via the `DimensionCanon`.
3. **Persist** — asset + dims + approval status are committed. It is now a permanent library item.
4. **Reuse** — every future build finds it. **Richness accumulates across sessions:** the first Strahd build exposes many gaps → we author + approve them → the next build is richer for free.

**At generation time, when an asset is still missing** (policy — never faking): substitute the nearest grounded
**same-category** asset and **log the downgrade**, or for function-critical items place a **flagged marker**;
either way append the gap to the **"wanted assets" backlog** ([`WantedAssetsBacklog.md`](../WantedAssetsBacklog.md)).
Never a silent fake, never a placeholder wearing the real thing's name.

**The Strahd mansion, concretely:** banquet hall = a room program (long high table + benches + **chandelier** +
wall art); oversized table = a furniture asset with a `size` tag; paintings/tapestries = a **decal / framed-picture
system — a current engine gap (flagged, not faked)**; busts/statues = voxel templates; ornate banisters = a
stair-detail asset the `place_stairs` placer applies; chandeliers = a lighting template + a point light via
`place_lights`.

**Honest current state:** the *mechanism* partly exists (`AssetLibrary` status/provenance, BlockSmith template
generation, the texture + dimension pipelines). The *content* is sparse (crude furniture; no art / sculpture /
chandelier / banister; weak textures), and a **wall-art decal system does not yet exist**. So the work ahead is
*filling* the library — but the architecture means we fill it **once** and the engine keeps it.

## Part 5 — Vertical / multi-level (basements, upper floors, attics)

**Current state (ground truth from the code).** The *schema* already models this — `BuildingProgram` has
`stories[]`, `ProgStair { fromStory, toStory }`, and `substructure = slab | crawlspace | basement`. The
*realizer does not*: `StructureRealizer::realizeShell` builds only `program.stories[0]` (line 71), **never
reads `ProgStair`** (no vertical circulation is built at all), treats `substructure == "basement"` as a
3-cube perimeter retaining **void** under the ground floor (`// basement body deferred to P5`), and emits a
**solid-shell gable roof** whose interior void is sealed, dark, and inaccessible — not a usable attic. So:
representable in the spec, **not realized**.

**The unlock is `stack_stories` (#36).** Iterate `stories` with a running base-Y, run the per-story placers
(floor / walls / openings / ceiling) at each level, then realize each `ProgStair` (#12) by cutting a hole
through the floor *and* ceiling slabs and building the flight. A **basement is a story at level −1**; an
**attic is a story inside the roof volume**. Everything else is a specialization of this loop.

**Basement** (`excavate_basement` #34 + `place_basement` #35): excavate the footprint box into terrain;
perimeter **retaining walls** (stone, ≥ the masonry exterior thickness — they hold back earth, *not* the thin
timber wall); base slab + drainage; headroom per use; access via an internal stair down (`ProgStair{0→−1}`) or
an external bulkhead; light via window wells (habitable) or vents (storage). Distinct from the existing
**crawlspace**, which is a correct *non-occupiable* shallow void.

**Attic** (`place_attic` #37): the roof placer defines the envelope; the attic floor = the top story's
ceiling joists. **Usable area = only the strip where headroom under the pitch ≥ 1.5 m** — the attic room is
*smaller than the footprint*, eaves excluded, knee walls closing the unusable wedge. Access via stair / ladder
/ hatch; light via gable windows or **dormers** (a roof-penetration feature we don't have yet — flagged).

### Grounded vertical dimensions

Human-clearance values cite the IRC (permitted by our rule for *clearance*, not period structure). Period
structural values that don't resolve to a clean standard are **flagged for the grounding-auditor**, not invented.

| Value | Grounded figure | Source |
|---|---|---|
| Habitable ceiling height (any story) | ≥ 2.134 m (7′0″) | IRC R305.1 |
| Storage / non-habitable basement headroom | ≥ 2.032 m (6′8″); beams may project to 1.931 m | IRC R305.1 + exception |
| Stair riser (max) | ≤ 0.196 m (7¾″) | IRC R311.7.5.1 |
| Stair tread depth (min) | ≥ 0.254 m (10″) | IRC R311.7.5.2 |
| Stair clear width (min) | ≥ 0.914 m (36″) | IRC R311.7.1 |
| Stair headroom (min) | ≥ 2.032 m (6′8″) | IRC R311.7.2 |
| Egress opening (habitable below-grade) | net ≥ 0.530 m² (5.7 sq ft), H ≥ 0.610 m, W ≥ 0.508 m, sill ≤ 1.118 m | IRC R310.2.1 |
| Egress window well | area ≥ 0.84 m² (9 sq ft), projection ≥ 0.914 m; ladder if depth > 1.118 m | IRC R310.3 |
| Attic usable floor (headroom mask) | count floor area only where headroom ≥ 1.5 m | RICS / loft-conversion surveying convention |
| Attic practical standing headroom | ≥ 2.0–2.2 m at the ridge zone | UK Approved Doc K loft practice |
| Medieval masonry wall build-up | two leaves dressed ashlar + rubble/mortar core | Undercroft (Wikipedia); Tracing the Past: Medieval Vaults |

**Flagged — NOT yet grounded (route through the grounding-auditor before any number is used in code):**
- **Medieval domestic cellar/undercroft headroom** — no clean period standard found (Clarendon's wine cellar is 34 m *long*, no height given). Engine floor = the IRC storage clearance (2.032 m); period undercrofts were often vaulted and taller — the domestic figure is unverified.
- **Medieval cellar retaining-wall thickness** — not independently grounded. Floor = the style's masonry exterior thickness; ≥ 0.667 m for stone (existing manor canon), scaling toward the castle 2–6 m canon when fortified. Precise domestic value unverified.
- **Medieval stair steepness** — IRC is the modern *comfort* floor; medieval domestic / newel stairs ran steeper and narrower. Period geometry unverified.
- **Knee-wall height** — currently *derived* from the 1.5 m headroom mask (not a constant); a fixed period value is unverified.

These figures are **design-of-record here** — they feed `structure_styles.json` (basement story height,
retaining-wall thickness, roof pitch → attic envelope) and `object_dimensions.json` (stair, window-well)
**when the placers are built**, and are not yet wired into runtime canon.

## Part 6 — Building-archetype library (the typology layer)

An **archetype** is a building-level typology — the building-scale analog of the Part 3 per-room programs.
It's what the LLM *picks* ("a wizard tower", "a dockside tavern"); the engine realizes it deterministically.
Each extends the existing `room_program.json` `programs` schema (which already holds four — `croft`,
`longhouse`, `hall_house`, `manor_hall`) and is persisted as data — another canon in the content library
(Part 4). No archetype lets a number stand un-grounded (the standing rule).

**Deep per-archetype data sheets** — threat model, access tiers, room program, adjacency rules, function testers,
grounding ledger — live in [`docs/structure-generation/archetypes/`](archetypes/). **Part 6 is the index; those are the depth.**
Security/function-defined types (bank, gaol, …) **require a sheet before any build** ([`bank`](archetypes/bank.md)
is the worked exemplar).

**Archetype schema (index level):**
| Field | Meaning |
|---|---|
| `id` / `function` | the type and what it's for |
| `extends` | base program it specializes (optional) |
| `form` | footprint shape (rect / L / round / courtyard / **compound**), story count, substructure, signature silhouette |
| `program` | rooms by Part 3 `purpose` + size (bays), required vs typical |
| `signature` | the features that make it *read* as this type |
| `function_test` | building-level "does it work as X" gate (composes the Part 3 room tests) |
| `scaling` | how status / period / region change it |
| `sources` / `to_ground` | per-value citations, or the flagged list routed to the grounding-auditor |

**Compound archetypes** (castle, monastery, cathedral) aren't single buildings — they *compose* sub-building
archetypes + the fortification (#31) and settlement/parcel placers. That composition logic is a settlement-tier
concern (not yet written) — flagged.

### The library

**Dwellings** — `croft`, `longhouse`, `hall_house`, `manor_hall` exist + cited (`room_program.json`). Additions:
- **`townhouse`** (urban burgage) — narrow gable-to-street frontage, 2–3 stories, **jettied** upper floors; ground-floor shop/workshop + hall + chambers above + rear kitchen/yard. *Signature:* narrow gabled front, jetty, mixed-use. *Test:* street-fronting commercial ground + private upper. *to_ground:* burgage frontage width.
- **`manor` / `ornate_house`** — wealth tier of `manor_hall` + solar, parlour, chapel, long gallery, gatehouse, gardens. *Cited via* `manor_hall`.
- **`slum_tenement`** — improvised: scavenged/patched materials, no foundation, subdivided rooms, lean-tos, street **encroachment**, no sanitation, overcrowded, fire-prone. *Signature:* squalor + encroachment + lean. *Test:* shelter only — **intentionally fails the quality tier** (a designed low tier, not a bug). *to_ground:* occupants/room density.

**Hospitality**
- **`tavern` / `inn`** — common room (bar + hearth + tables/benches + casks), kitchen, drink cellar, guest chambers (beds), innkeeper's quarters, + stable yard (coaching inn). *Signature:* hanging sign, large common room, stable yard. *Test:* bar + seating + hearth + drink storage + lodging + route to cellar/kitchen. *to_ground:* common-room size, guest-room count.

**Commerce — the shop family** (each = street shopfront + workshop + storage + dwelling above):
- **`blacksmith`** — forge + anvil + bellows + quench + tool rack + workbench + fuel; fire-safe / detached. *(forge program, Part 3.)*
- **`apothecary`** — counter + jar/herb shelving + workbench + drying loft + back store.
- **`bakery`** — oven + chimney + kneading table + flour store + shopfront; fire-safe.
- **`butcher`** — counter + block/hooks + cold store + rear slaughter yard (downwind/downstream).
- **`tailor` / `weaver`** — loom/work table + cloth store + strong light + counter.
- **`cooper` / `carpenter`** — workbench + timber store + tool rack + yard.
- **`tanner`** (noxious) — pits + drying racks, sited at the edge & downstream (zoning — settlement tier).
- *Common signature:* pictorial trade sign (illiterate clientele), shutter-down street counter, mixed-use above. *Test:* the trade's required station (Part 3) + customer counter + storage. *to_ground:* shopfront width, counter dims.

**Civic**
- **`guildhall`** — meeting hall + offices + store; often a ground-floor market arcade.
- **`town_hall` / `moot_hall`** — assembly hall raised over an open market floor; bell.
- **`gaol`** — cells (barred/locked) + guardroom + yard.
- **`warehouse`** — open storage volume + cart/loading doors + (dockside) crane.
- **`mill`** (water/wind) — millstones + drive + hopper + meal bins + sacks + hoist. *(mill program, Part 3.)*
- *to_ground:* civic hall sizes.

**Faith**
- **`shrine`** — altar + idol/icon + offering space.
- **`church` / `chapel`** — nave + chancel (altar, **oriented east**) + tower/bell + porch.
- **`temple`** (pantheon) — cella + cult statue + portico + precinct.
- **`cathedral`** *(compound)* — church + transepts + aisles + crypt + chapter house + cloister.
- **`monastery`** *(compound)* — church + cloister + refectory + dorter + chapter house + infirmary + guesthouse + gatehouse.
- *Cited:* east orientation + nave/chancel/sacristy from Part 3. *to_ground:* church proportions.

**Power / fortified** *(dimensions grounded this session)*
- **`tower_house` / `wizard_tower`** — compact vertical keep: vaulted cellar (storage) → stacked single chambers → parapet roof; **first-floor entrance** (defensive); thick walls; barmkin yard. *Grounded:* Henry VI 1429 statute min **6.1 × 4.9 × 12.2 m**; walls **6 ft (1.83 m) below the vault, 4 ft (1.22 m) above**; round example (Balief) ~**10.7 m tall, 4.78 m interior dia, 2.54 m walls**. **Wizard tower** = `tower_house` + an arcane program (laboratory / library / observatory / summoning-circle) — the *fantasy overlay deferred to the setting-canon tier* (flagged).
- **`keep` / `great_tower`** — the castle's strongest building: great hall + chambers + chapel + well + dungeon over a vaulted basement; first-floor entrance. *Grounded:* Dover **29.5 m square, 25.3 m tall, walls to 6.4 m**; Pembroke round keep **16 m dia, 24 m tall**; shell-keep wall **3–3.5 m thick, 4.5–9 m high**; general keep walls **2–4 m** *(audit-corrected: 1.5 m belongs to a fortified manor, not a keep — sources start keeps at ~2 m)*.
- **`castle`** *(compound)* — curtain wall (2–6 m, existing canon) + flanking towers + gatehouse/barbican + keep + bailey buildings (great hall, chapel, stables, smithy, kitchen, barracks) + moat/ditch + dungeon below. Composes `place_fortifications` (#31) + `keep` + bailey archetypes + the subterranean tier.

#### BG3-Act-3 gap-fill *(mostly `to_ground`; genre-flagged where post-medieval/fantasy — see CC7)*

**Commerce — additions**
- **`arcane_emporium`** — magic *retail*: a shop floor of curios/scrolls/wands + a counter + a warded back-vault + proprietor's quarters/tower above (multi-story). *Signature:* glowing wares, arcane sign, warded door. *Test:* counter + magical-goods display + secure store. *Genre:* the retail magic shop is a **fantasy conceit** — structurally a `townhouse`. *to_ground:* size.
- **`general_store` / `trading_post`** — general goods: counter + dense shelving + crates/barrels + back store. *Test:* counter + broad storage. *to_ground:* size.

**Finance & institutions** *(new)*
- **`counting_house` / `bank`** — counting room + a **strongroom/vault** (thick walls, locked, usually cellar) + clerks' desks + a controlled entrance. *Signature:* heavy door, barred windows, vault below. *Test:* secure vault + counting floor + controlled entry. *Genre:* the bank-as-institution is **late-medieval/early-modern** (Italian banking houses) — flag for a strict-medieval brief. *to_ground:* vault wall thickness (reuse the keep / retaining-wall canon).

**Entertainment & vice** *(new)*
- **`brothel` / `festhall`** — a parlour/common room + private chambers + a madam's room (often bath-adjacent); discreet, decorated. *Test:* reception + private rooms + privacy. *to_ground:* size.
- **`theatre` / `playhouse`** — stage + tiered galleries + tiring-house (backstage) + a yard. *Genre:* permanent playhouses are **early-modern (Elizabethan)**, not medieval — flag. *to_ground:* stage/yard dims.
- **`costumier` / `disguise_shop`** — shop counter + costume racks + fitting room + workshop. *Test:* display + fitting + workshop.
- **`gambling_den`** — gaming floor + tables + a bar + a back room; often illicit/hidden. *Test:* gaming floor + drink + discreet access.

**Civic — additions**
- **`civic_palace` / `seat_of_state`** — grand audience hall + council chamber + offices + private apartments + a strongroom; the seat of government (a ducal palace), larger than `town_hall`'s moot hall. *Test:* audience hall + governance rooms + security. *to_ground:* hall scale (reuse `manor_hall` / great-hall proportions).
- **`printing_house` / `news_office`** — press room + type-setting + paper store + public counter. *Genre:* the printing press is **post-1450 (early-modern)** — flag. *to_ground:* press-room size.
- **`bathhouse` / `stews`** — heated bathing hall + tubs/plunge + furnace + changing rooms + water supply + drainage. *Test:* heated water + bathing space + drainage. *to_ground:* hall size. *(Medieval "stews" existed; the grand Roman bath is bigger/anachronistic.)*
- **`hospital` / `hospice`** — an **infirmary hall** (aisled, beds in rows) opening onto a **chapel** at one end + a dispensary + a warden's lodging. *Grounded form:* the medieval hospital = an infirmary hall open to a chapel. *Test:* bed hall + chapel + dispensary. *to_ground:* hall dims.
- **`mortuary` / `mausoleum`** — a laying-out room, or a freestanding tomb-house over/beside a crypt. *Test:* body handling/interment + dignified form. Links Part 8 `place_crypt` + #32 `place_graveyard`.

**Industry** *(new)*
- **`foundry` / `manufactory`** — a furnace/forge hall at scale + casting floor + bellows/water power + material yards + a chimney/flue; loud, smoky, fire-managed. *Signature:* big chimney, furnace glow, industrial massing. *Test:* furnace + casting + material flow + venting + fire safety. *Genre:* a true manufactory/foundry is **early-modern/industrial** (the BG3 Steel Watch Foundry is overtly fantastical) — flag. *to_ground:* furnace/hall dims.

**Maritime** *(new — blocked on the water/shoreline engine feature + a Part 7 waterfront sub-tier, both unbuilt)*
- **`wharf` / `pier` / `quay`** — decked platform along/over water for mooring + loading; bollards, cranes, steps.
- **`harbormaster` / `customs_house`** — a quay-side control office: counting room + watch point + bonded store.
- **`fish_market`** — open stalls/slabs + a hall + heavy drainage; waterfront.
- **`lighthouse` / `beacon_tower`** — structurally a `tower_house` + a lantern room (maps to the `glow` material) at the harbour mouth.
- **`boathouse` / `shipyard`** — covered slip + building/repair yard + timber/pitch stores.
- *Engine note:* the whole maritime group **cannot be built until water/shoreline exists** — flag, don't fake.

**Style & condition overlays (NOT archetypes)** — some BG3 buildings are an existing archetype + an overlay:
- **Gothic-horror palace** (a vampire's seat) = `ornate_house` + a crypt (Part 8) + a **gothic-horror style overlay** (bone/iron/dark stone) — a *style*, barely supported today (flag).
- **Derelict / haunted mansion** = `ornate_house` + a heavy **condition overlay** (decay/ruin — category J).
- **Fortified bridge-prison** (gate-fortress on a crossing) = `compose_compound` of `castle` + `place_bridges` (#44) + `gaol` — composition via the unbuilt settlement tier.

All gap-fill dimensions are `to_ground` unless they reuse a grounded canon (vault → keep/retaining; civic hall → `manor_hall`; lighthouse → `tower_house`). Post-medieval/fantasy types (bank, theatre, printing house, foundry, arcane shop) are **genre-flagged per CC7**; the maritime group is **engine-blocked on water**. New materials these need are tracked in [`WantedAssetsBacklog.md`](../WantedAssetsBacklog.md).

*Sources (this session):* tower house — [Wikipedia: Tower house](https://en.wikipedia.org/wiki/Tower_house), [Tower houses in Britain and Ireland](https://en.wikipedia.org/wiki/Tower_houses_in_Britain_and_Ireland); keep — [World History Encyclopedia: Castle Keep](https://www.worldhistory.org/Castle_Keep/), [Bergfried](https://en.wikipedia.org/wiki/Bergfried), [Round Keep Castles](https://www.medieval-spell.com/Round-Keep-Castles.html).

**Honest grounding status:** form / program / signature are qualitative and citable to architectural history;
the marquee dimensions (tower house, keep, manor/great hall) are cited above; every other per-archetype size
sits in its `to_ground` list and routes through the grounding-auditor before a number enters code. The library
is **design-of-record** — it extends `room_program.json` into a `structure_archetypes.json`, not yet wired into
runtime canon, and the **compound-composition + fantasy-overlay logic is unbuilt** (flagged).

## Part 7 — Settlement tier (towns, cities & compounds)

The tier *above* the building. A settlement is a **graph of plots on a street network**, within an optional
wall, zoned into **districts**, served by a **public realm** + utilities, and **populated by Part 6 archetypes**.
This is where the "compact city" lives — and where **compound archetypes** (castle, monastery, cathedral)
resolve, since each is a walled mini-settlement of sub-buildings. Growth is **accretive-first** (the model you
chose): seed a feature (crossing / market / castle), grow the street net + plots outward, wall it when it
matters. Nothing here is built yet — it's the target spec for placers #38–49.

**The pipeline** (each runs once per settlement, then `populate_plots` invokes the whole building pipeline #1–37 per lot):

`site_settlement` (#38) → `lay_street_network` (#39) → `subdivide_plots` (#40) → `zone_districts` (#41) →
`place_town_wall` (#42) + `place_bridges` (#44) → `place_public_spaces` (#43) → `populate_plots` (#45) /
`compose_compound` (#46) → `place_signage` (#47) + `dress_street_life` (#48) → `link_subterranean` (#49, handoff).

**Why-here, then how-grown.** `site_settlement` justifies the location (river crossing, defensible spur,
harbour, crossroads, resource) — the same "no arbitrary placement" rule as building siting, one scale up. The
street net then grows **organic** (curving lanes accreting around the feature) or **planned** (a bastide grid),
with the **market at the main crossing** and the **high street linking gate → market**. Plots are **burgage**:
narrow street frontage, deep back-plot, party walls in the core, subdivided in fractional-perch units.

**Districts carry the social truth.** `zone_districts` assigns wealth + trade per quarter: high near the
centre / castle / minster, low at the periphery / marsh / against the wall; trades **cluster** (a smiths'
street, the shambles) and **noxious trades** (tanners, dyers, slaughter) go to the edge, **downwind +
downstream** of dwellings and the water intake. This is what makes a slum read as a slum and a rich quarter
read as wealth — it drives which Part 6 archetype + status tier lands on each plot.

**Does the city function?** `AA` is the settlement-scale analog of the per-room function testers: water, food,
waste, defense, governance, worship, trade, production, circulation — each must be *present and reachable* for
the population, or the settlement fails its function test.

### Grounded settlement dimensions

| Value | Grounded figure | Source |
|---|---|---|
| Perch (the planning unit) | 5.5 yd ≈ 5.03 m | standard medieval rod/pole/perch |
| Burgage plot (standard) | 3–3.5 × 12 perches = **16–18 m × 60 m** (~¼ acre) | post-Conquest town charters |
| Burgage frontage (narrow / subdivided) | 2 perches ≈ **10 m**, split in fractional-perch units | Cricklade; Tewkesbury (4 × 40 perch primary, subdivided) |
| Town / curtain wall height | ~**9 m (30 ft)** or more | medieval fortification norm |
| Town / curtain wall thickness | **2.5–6 m** (siege minimum ~2.1–2.4 m) | medieval fortification |
| Town gate (foregate example) | **6.8 × 9.5 m** | [Byczyna German Gate foregate (medievalheritage.eu)](https://medievalheritage.eu/en/main-page/heritage/poland/byczyna-city-defensive-walls/) *(single source; page not independently re-fetchable)* |
| Large market square (exceptional) | **3.79 ha / 9.4 acres** | Kraków Main Square (13th c.) |
| Population density | ~**40–61 people/acre** | RPG demographics (S. John Ross lineage, from historical estimates) |
| Building density | ~**20–30 structures/acre** | same |
| Town extent | usually **< 1 sq mile (640 acres)** | same |

**Flagged — NOT cleanly grounded (route through the grounding-auditor before any number enters code):**
- **Street metric widths** (high street / lane / alley) — the *regulations* are cited (the *via Regia* "wide
  enough for two wagons to pass"; a town street passable by "a horseman with a lance across his saddle"), but no
  clean metric. Working derivation: high street ≈ two carts passing, lane ≈ one cart, alley ≈ foot-only — flagged.
- **Typical market-square size** — Kraków is exceptional; an ordinary market square is far smaller. Typical figure unverified.
- **Town wall vs castle curtain** — towns were often thinner/lower than the curtain range above; the town-specific figure is unverified (uses the curtain range as a ceiling).
- **Population-density figures** — the 40–61/acre + 20–30 structures/acre come from RPG worldbuilding (derived from historical estimates), not a primary survey; usable as a planning figure, flagged as such.

*Sources (this session):* [Burgage plot (Kiddle)](https://kids.kiddle.co/Burgage), [Wiltshire community history: burgage plots](https://apps.wiltshire.gov.uk/communityhistory/Question/Details/216); [Medieval fortification (Wikipedia)](https://en.wikipedia.org/wiki/Medieval_fortification), [Byczyna town walls](https://medievalheritage.eu/en/main-page/heritage/poland/byczyna-city-defensive-walls/); [Main Square, Kraków (Wikipedia)](https://en.wikipedia.org/wiki/Main_Square,_Krak%C3%B3w); [Medieval town size (EN World)](https://www.enworld.org/threads/area-of-a-medieval-town.255833/).

**Honest status:** the whole tier is **unbuilt** — design-of-record only. It depends on the building pipeline
(#1–37, itself mostly target spec) and the archetype library (Part 6, not wired). The **subterranean handoff**
(#49) leads into a tier that needs terrain excavation + multi-level connectivity we don't have. None of this
realizes a voxel yet; it's the map for getting there.

## Part 8 — Subterranean tier (sewers, crypts, dungeons, mines)

The layer *below* the building — the D&D dungeon-crawl staple. Six kinds: **sewers**, **crypts/catacombs**,
**castle dungeons/oubliettes**, **mines**, **smuggler/secret tunnels**, and **natural caves**, optionally
woven into one navigable graph that connects cellars, the sewer net, the crypt under the minster, and the
dungeon under the keep.

**Honest framing first.** Two things must be said plainly:
1. **This is the biggest engine gap in the whole document.** It needs two capabilities the engine *does not
   have*: (a) **real terrain excavation** — carving voids into chunk terrain and removing/backfilling earth
   (the same gap that makes the Part 5 basement a 3-cube stub), and (b) **multi-level void connectivity** — a
   navigable graph + nav-mesh across stacked underground levels. Until those exist, this tier can't place a
   voxel. `excavate_subterrane` (#50) and `connect_underground` (#55) are flagged **engine-cap-missing**, not
   merely unwritten placers.
2. **Walkable sewers and dungeons are anachronistic for a strict medieval brief.** Medieval towns used
   **cesspits + open street drains**, not monumental walkable sewers (those are *Roman* — the Cloaca Maxima —
   and *Victorian*). Big crawlable dungeons are largely a **game convention**. For a D&D world that's fine and
   wanted — but the model **flags it as a conceit** (BB9), grounding the dimensions to the real precedents
   (Roman sewers, mine adits, catacombs) rather than pretending they're typical medieval.

**The pipeline:** `excavate_subterrane` (#50) → the kind-specific carvers — `carve_sewer_network` (#51),
`place_crypt`/`place_catacomb` (#52), `excavate_dungeon` (#53), `place_mine` (#54), `place_secret_passages`
(#56) → `connect_underground` (#55) → `validate_crawlability` (#57, the playability gate). Each carver follows
its own logic: sewers run by **gravity to a river outfall** under the streets and take the garderobe chutes;
mines follow an **ore seam** (adit → gallery → shaft) and dump spoil at the mouth; crypts sit **under the
chancel** with body-sized loculi; dungeons sit **under the keep**, grim and secure.

### Grounded subterranean dimensions

| Value | Grounded figure | Source |
|---|---|---|
| Vaulted walkable sewer | **2.7–3.3 m high × 2.1–4.5 m wide**, barrel-vaulted | Cloaca Maxima (Roman) — *audit-corrected: 4.5 m is the outlet width, not height* |
| Catacomb gallery | ~**2.5 m high × ~1.0 m wide** (walk upright) | Catacombs of Rome |
| Loculus (burial niche) | **0.4–0.6 m high × 1.2–1.5 m long** | Catacombs of Rome |
| Catacomb depth | first level **3–8 m**; up to **20–25 m** over 4–5 levels | Catacombs of Rome |
| Mine adit / gallery | **~2.3 m high × ~1.14 m wide** (≈ 2:1), one miner | CITED — Agricola, *De Re Metallica* Bk V: a tunnel "nearly twice as high as broad", **1¼ fathoms high × ~3¾ ft wide** |
| Cramped crawl tunnel (Erdstall) | **1.0–1.4 m high × ~0.6 m wide** | Erdstall |
| Walk-upright passage clearance (min) | ≥ **2.032 m headroom × ≥ 0.914 m wide** | IRC R311.6 / R305 (anthropometric) |

**Flagged — NOT cleanly grounded / convention (route through the grounding-auditor):**
- **Medieval town sewer** — a walkable sewer is Roman/Victorian; medieval norm was cesspits + open kennels. Dimension borrowed from the Cloaca Maxima and **flagged anachronistic** for a medieval brief.
- **Dungeon corridor / room sizes** — the "10 ft (3 m) corridor" is **D&D game convention**, not a historical standard; flagged as convention.
- **Smuggler / secret-tunnel dimensions** — unverified.

*Sources (this session):* [Cloaca Maxima (Wikipedia)](https://en.wikipedia.org/wiki/Cloaca_Maxima); [Adit (Wikipedia)](https://en.wikipedia.org/wiki/Adit), [Erdstall (Wikipedia)](https://en.wikipedia.org/wiki/Erdstall); [Catacombs of Rome (Wikipedia)](https://en.wikipedia.org/wiki/Catacombs_of_Rome), [International Catacomb Society](https://www.catacombsociety.org/the-structures-of-the-catacombs/).

**Honest status:** unbuilt **and engine-blocked**. This is the tier to reach for only after terrain excavation
+ multi-level connectivity exist — and those two are the highest-value *engine* features the structure-gen
roadmap needs (they also unblock the Part 5 basement and the Part 7 `compose_compound` dungeons). Documented
here so the requirement is explicit, not so it looks done.

## Part 9 — Fantasy / setting-canon tier (the world bible)

The **cross-cutting** axis. Parts 1–8 assume a real-world (medieval) frame and ground every dimension to real
history. A D&D world is fantasy — wizard towers, gods, magic, non-human races. **The honesty rule does not
relax here; it re-targets.** A fantasy value grounds to the **world bible** instead of reality: invent once,
cite the bible, validate for internal *consistency* (not realism), apply everywhere. "Source-or-stop" still
holds — the source is just the bible.

**The world bible** is a persisted setting canon (`world_bible.json`, per-world — like the existing world
recipe in the `world_meta` table) and another canon in the content library (Part 4). It codifies:
- **Magic system** — what magic does / can't do, its costs, taboos, material expressions.
- **Magical materials** — arcane stone, everbright crystal, coldiron — with consistent properties; extends `materials.json` and **maps magical light onto the existing `glow` emissive material**.
- **Pantheon** — deities, domains, iconography, sacred colours / symbols / geometry, temple orientation; drives the `temple` / `shrine` / `church` archetypes.
- **Races & cultures** — dwarven carved-stone halls, elven grown living-wood, orcish crude bone/hide, halfling earth-bermed smials, gnomish mechanisms — each a construction logic + material palette + form.
- **Factions & orders** — mage guilds, knightly orders, thieves' guilds and their architectural signatures.
- **Era / tech level** — the setting's "period" can be non-historical; for a fantasy brief, **the period gate (category A) cites the bible, not real history.**

**How grounding works per setting:**
- **Real-world brief** → cite real-world sources (the canons in Parts 5–8).
- **Fantasy brief** → cite the **world bible**. The bible's *own* entries are authored once (LLM/user),
  validated for **internal consistency**, user-approved, and persisted — the same content-library
  generate-once-persist flow (Part 4). Every fantasy value then traces to a bible rule.
- **Consistency validator** — a value must trace to a bible entry and not contradict another (a sun-god's
  temple can't face away from sunrise if the bible says east; living-wood elves can't build in cut ashlar).

**Fantasy is an OVERLAY on grounded structure.** The skeleton stays real: a **wizard tower is structurally a
`tower_house`** (Part 6 — grounded 6.1 × 4.9 × 12.2 m, thick walls, first-floor entry); the *fantasy* is the
overlay — an arcane room program (laboratory / library / observatory / summoning circle / planar anchor),
magical materials, glowing light. A temple is a grounded `church`/`temple` shell + the pantheon's iconography.
This keeps fantasy buildings **buildable and believable**, not physics-defying — *unless* the bible explicitly
licenses the impossible (a floating tower, a bigger-on-the-inside vault), which then becomes an **engine-gap
flag**: non-Euclidean interiors, floating mass, and gravity-defying spans are **not engine-supported** — flag,
don't fake (CC8).

### Grounding note

This tier introduces almost no new real-world dimensions — the *structure* stays grounded through Parts 5–8;
its contribution is the **bible-as-source mechanism** + the **consistency validator** + the **overlay system**.
The mechanism's provenance is the established practice of the **story / world bible**, whose entire purpose is
enforcing internal consistency across a fictional setting. Magical light maps to the **real `glow` material**
that already exists in `materials.json`.

*Sources (this session):* [Worldbuilding Bible (Dabble)](https://www.dabblewriter.com/articles/worldbuilding-bible); [Creating a World Building Bible (Author Learning Center)](https://www.authorlearningcenter.com/writing/fiction/w/setting/6803/creating-a-world-building-bible-to-set-the-rules-and-maintain-consistency-in-your-novel).

**Honest status:** unbuilt. The bible schema, the consistency validator, the period-gate-cites-bible rewiring
of category A, and the overlay system are all target spec. The one piece that exists is the `glow` material
(magical light has a real home). Everything else — including which impossibilities the engine can ever support —
is documented as requirement, not as done.

## Part 10 — The build pipeline (brief → build derivation)

The **glue**: how a filled `StructureBrief` becomes built voxels **deterministically**, with the LLM only
authoring *intent* and *approving assets*. The engine **derives and builds**; it does not improvise. This is the
spine of the standing rule — *the engine generates, not Claude.*

```
StructureBrief        →  [validate]  →  DERIVE AssemblyPlan  →  [validate]  →  run placers (#1–59,   →  voxels
(LLM-authored intent)                    (engine, deterministic)                selected subset, in order;
                                                                                 each gated by Part-2 checks)
```

### Stage 1 — The `StructureBrief` (intent only)
The ~43-field mandatory intake (`docs/structure-generation/StructureBrief.md` / the engine-resident schema, E1). Grouped:
**setting/period (the FIRST gate)**, function, status/wealth, scale/footprint, site (slope / water / approach /
orientation), region + climate, materials-availability, condition/age, defensibility, faith, parcel/context, and
(fantasy) a world-bible ref. The LLM fills this; **no voxel exists yet**. Blocking fields must be answered;
the rest take **cited defaults** (warn-but-allow).

### Stage 2 — Derivation (deterministic, engine)
Each step is a rule/table, not a judgement call:
1. **Archetype** ← (function, setting, status, urban/rural) → a Part 6 archetype + scaling tier.
2. **Style** ← (setting/period, region, status, materials-availability) → a `StyleProfile` (wall material + thickness, roof material + pitch, foundation) from `structure_styles.json`.
3. **Room program** ← (archetype, status, footprint) → the bay model + required/typical rooms (Part 3 + `room_program.json`).
4. **Site fit** ← (slope, water, grade) → `prepare_pad` / foundation / basement decisions.
5. **Placer subset + order** ← (archetype, parcel?, defensibility?, basement/sewer?, religious?, season) → which of #1–59 run vs skip.
6. **Fantasy overlay** ← (setting = fantasy) → world-bible overlays (Part 9).

Output: the **`AssemblyPlan`** — the fully resolved (archetype, style, program, site-fit, placer-list, overlays),
physical-ready, with every dimension traced to a grounded canon.

### Stage 3 — Realization
Run the selected placers **in order**; each emits voxels/objects and is **gated by its Part-2 checklist items**.

### The decision tables (the deterministic core — design-of-record)
| Input | → | Output | Source |
|---|---|---|---|
| function × status (× urban/rural) | → | archetype | Part 6 |
| (setting, region, status, materials) | → | style (cruck+wattle&daub+thatch ↔ ashlar+tile ↔ cob …) | `structure_styles.json` |
| archetype × status × footprint | → | room program (bays + required/typical rooms) | Part 3 + `room_program.json` |
| archetype + flags | → | placer subset + order | Part 1 |

**Conditional-placer triggers** (when a placer runs at all):
- parcel placers (#21–29) — iff a **lot/plot exists** (rural or a yarded urban plot), not for a party-wall townhouse.
- `place_fortifications` (#31) — iff **defensibility > none**.
- subterranean (#50–57) — iff **basement | sewer | crypt | dungeon** in the program.
- `place_graveyard` (#32) — iff **religious + burial**.
- settlement tier (#38–49) — iff building **a settlement**, not a single structure.
- `apply_seasonal_state` (#33) — **always last**, driven by the season / time-of-day fields.

### Where the LLM is allowed (and where it is not)
- **LLM:** authors the brief (high-level intent), approves generated assets, may *suggest* design — never lays a voxel, never invents a dimension.
- **Engine:** all derivation + all physical generation, deterministically from the canons. *(The standing rule, made mechanical.)*

### Validation gates (warn-but-allow)
`StructureBriefValidator` (complete + legal + period-coherent) → `BuildingProgramValidator` (derived program legal:
topology / reachability / scale / typology) → `AssetValidator` (assets exist + approved) → per-placer checklist
gates (Part 2). Failures **log and warn**, they don't hard-block (the chosen policy) — but a *blocking* brief field
(e.g. a missing period) stops at Stage 1.

### Honest status
**Implemented:** the `StructureBrief` schema (E1, engine-resident), `BuildingProgramValidator` (partial),
`FurniturePlacer`. **Target / design-of-record:** the derivation engine, the decision tables above, the
`AssemblyPlan` resolver, and 58 of 59 placers. The pipeline is specified here; almost none of Stage 2 is code yet.

---

### Coverage today (honest)
Implemented + enforced: a slice of **D** (no-overlap by construction), **E** (wall thickness, ceiling,
room-area/proportion typology gate), **G2/G3** (reachability, door size), parts of **K** (furniture
purpose/facing/clearance/on-floor via `FurniturePlacer`), **A/J** material grounding, **O** (brief +
provenance). **Most of A, B, C, F, H, I, L, M and large parts of D, G, K, N are NOT yet implemented.**
This document is the target; build the placers (Part 1) and wire each check (Part 2) as gates.
