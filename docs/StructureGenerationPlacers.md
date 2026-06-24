# Structure Generation — Placers & Quality Checklist

> The architecture for grounded, rich, believable, functional structures: a pipeline of small,
> single-purpose **placers** (one algorithm, one job), and the granular **checklist** every generated
> structure must satisfy. Companion to `docs/StructureGenerationV2.md` (overall design) and
> `docs/StructureBrief.md` (the intake). Standing rules apply: every dimension is grounded/cited (no
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
| 3 | `place_foundation` | footings to bearing (stepped on slope) + foundation/plinth walls + slab/crawlspace/basement | **P** (crawlspace ring) |
| 4 | `place_subfloor` + `place_floor` | structural floor + finish floor per room (material by status) | **P** (one slab) |
| 5 | `generate_room_layout` | derive room rects from typology + bay model + program (replace hand-authoring) | **M** |
| 6 | `place_exterior_walls` | perimeter walls, style thickness, exterior grade | **P** (fused) |
| 7 | `place_interior_walls` | partitions on shared boundaries, thinner | **P** (fused) |
| 8 | `cut_openings` | carve door/window/arch + sills + reveals + lintels | **P** (gaps only) |
| 9 | `place_doors` | door leaves, correct handedness/swing, register with `DoorManager` | **M** |
| 10 | `place_windows` | glazing / shutters / boards / open per period+status | **M** |
| 11 | `place_ceiling` / `place_intermediate_floor` | ceiling or upper-story floor; defer stairwell holes | **P** (fused) |
| 12 | `place_stairs` | stairs between stories / to cellar, with landings + headroom | **M** |
| 13 | `place_roof` | gable/hip/valley over the real outline + eaves/fascia/soffit + ridge + attic void | **P** (gable on rect, blocky, floats) |
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
either way append the gap to a **"wanted assets" backlog**. Never a silent fake, never a placeholder wearing the
real thing's name.

**The Strahd mansion, concretely:** banquet hall = a room program (long high table + benches + **chandelier** +
wall art); oversized table = a furniture asset with a `size` tag; paintings/tapestries = a **decal / framed-picture
system — a current engine gap (flagged, not faked)**; busts/statues = voxel templates; ornate banisters = a
stair-detail asset the `place_stairs` placer applies; chandeliers = a lighting template + a point light via
`place_lights`.

**Honest current state:** the *mechanism* partly exists (`AssetLibrary` status/provenance, BlockSmith template
generation, the texture + dimension pipelines). The *content* is sparse (crude furniture; no art / sculpture /
chandelier / banister; weak textures), and a **wall-art decal system does not yet exist**. So the work ahead is
*filling* the library — but the architecture means we fill it **once** and the engine keeps it.

---

### Coverage today (honest)
Implemented + enforced: a slice of **D** (no-overlap by construction), **E** (wall thickness, ceiling,
room-area/proportion typology gate), **G2/G3** (reachability, door size), parts of **K** (furniture
purpose/facing/clearance/on-floor via `FurniturePlacer`), **A/J** material grounding, **O** (brief +
provenance). **Most of A, B, C, F, H, I, L, M and large parts of D, G, K, N are NOT yet implemented.**
This document is the target; build the placers (Part 1) and wire each check (Part 2) as gates.
