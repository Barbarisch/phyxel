# Medieval era reference — measured dimensions, reference images, text-to-3D prompts

Research pass 2026-11-21 (web + file reads only; nothing built, nothing launched, no existing file edited).
Scope: north-west European medieval (11th–15th c.), the typologies shipped in `resources/room_program.json`
and `resources/settlement_program.json`. Units: metres (1 cube = 1 m). Conversions from feet/perches are mine
(1 ft = 0.3048 m; statute perch = 16.5 ft = 5.03 m) and are marked `→`.

**READ-STATUS key**

| Tag | Meaning |
|---|---|
| **PR-P** | page-read, primary or measured source: I had the text of the report/inventory itself (HTML or PDF text layer) and the quoted words are in it |
| **PR-T** | page-read, tertiary: I read the page, but the page (Wikipedia, a museum or wiki page) is quoting a source I did not open |
| **SNIP** | search-result snippet only — the page was not opened or did not open. Not to be typed as a value |
| **NO SOURCE** | looked, found nothing citable |

Two cautions that apply to everything below:
1. **Images were NOT viewed.** Section 2 lists Wikimedia Commons files chosen from the title, description, pixel size and
   licence the Commons API returned. Whether a shot is an unobstructed 3/4 view has to be checked by eye before feeding Meshy.
2. **No measured elevation, section or eaves/ridge height of any standing timber house was retrieved.** Storey, eaves and
   ridge heights are the largest hole (section 4a).

---

## 0. What the shipped data already labels as unsourced (medieval only)

Read from `resources/settlement_program.json` (medieval tiers), `resources/structure_styles.json`
(`timber_cottage`, `stone_manor`), `resources/room_program.json`, `docs/structure-generation/GroundingGaps.md`,
generated `DimensionReference.md`. `docs/WorldForgeV2Plan.md` §10 holds roads / terrain / hydrology numbers only —
it has no medieval building dimension, so nothing there is repeated here.

| File · key | Shipped value | Its own label |
|---|---|---|
| settlement · hamlet `buildings` | 3–6 | REASONED, NEEDS-RESEARCH (Roberts & Wrathmell 2000) |
| settlement · hamlet/village/town/city `typology_weights`, `core_typology_weights`, `typology_caps` | ratios | REASONED |
| settlement · hamlet `plot.depth` | 10–14 | DESIGN (scale-compressed) |
| settlement · hamlet `street.material` | Dirt | REASONED |
| settlement · all tiers `bells.*` | various | REASONED, NEEDS-RESEARCH (D40) |
| settlement · hamlet/village `crofts` (any length, width, ratio) | none typed | NO SOURCE; fence stand-in DESIGN |
| settlement · village `buildings` | 6–14 | REASONED, NEEDS-RESEARCH |
| settlement · village `street.main_width` | 5 | NEEDS-RESEARCH ("shipped FLAGGED") |
| settlement · village/town/city `street.lane_width` | 3 | NEEDS-RESEARCH, REASONED from cart gauge |
| settlement · village `street.material` | Gravel | REASONED |
| settlement · village `plot.depth` | 12–20 | DESIGN (scale-compressed) |
| settlement · village/town/city `fences.fraction` | 0.85 / 0.7 / 0.5 | REASONED |
| settlement · village `public.well` | true | REASONED |
| settlement · town `street.main_width` | 6 | NEEDS-RESEARCH |
| settlement · town `buildings` | 15–30 | REASONED, NEEDS-RESEARCH (Beresford) |
| settlement · town `public.market_square` | 12 × 12, 4 stalls | REASONED placeholder; stall count REASONED |
| settlement · city `street.main_width` | 7 | NEEDS-RESEARCH |
| settlement · city `blocks` | 18–30 | REASONED, NEEDS-RESEARCH (Conzen) |
| settlement · city `core_ring` | 24 | REASONED, "no measured source" |
| settlement · city `buildings` | 20–48 | REASONED |
| settlement · city `public.market_square` | 16 × 16, 6 stalls, statue | stalls REASONED; statue DESIGN |
| settlement · city `walls` height / thickness | 7 / 2 | REASONED picks "inside the attested band ~6–10 m, ~1.5–3 m" (band itself uncited) |
| settlement · city `walls.tower_size` | 9 | "Conwy's own scale" — no citation given for a Conwy tower diameter |
| settlement · city `walls.gate_width`, `margin`, `tower_extra_height` | 7 / 2 / 3 | no label for a source |
| styles · `timber_cottage` interior_wall / floor / ceiling | 0.111 / 0.333 / 0.111 | INFERRED |
| styles · `timber_cottage` `roof.overhang` | 0.4 | FLAGGED modern-analog, NEEDS-RESEARCH |
| styles · `timber_cottage` / `stone_manor` `ceiling` | 2.5–3.0 / 3.0–4.5 | ANTHROPOMETRIC (not period) |
| styles · `stone_manor` interior_wall / foundation_wall / floor / ceiling | 0.333 / 0.667 / 0.333 / 0.333 | INFERRED |
| styles · `stone_manor` `roof.overhang` | 0.6 | FLAGGED, NEEDS-RESEARCH (exceeds its own cited range) |
| room_program · `bay_length` (croft, longhouse, hall_house, all shops, tavern, tenement, town_hall) | 4 | DERIVED (longhouse 15–18 m ÷ 4), "Brunskill — TBD" |
| room_program · croft `entrance` position | wall middle | NEEDS-RESEARCH |
| room_program · hall_house / all `windows` size + count | 1 × 1, per_bay 0.5–1.0 | STYLIZED, NEEDS-RESEARCH |
| room_program · tavern `bays`, room split | 4 (2+1+1) | DESIGN (analogy to hall_house; Pantin 1961 unread) |
| room_program · blacksmith / general_store / bakery / apothecary / butcher `bays` | 2 | DESIGN (analogy) |
| room_program · blacksmith `width_min` | 5 | DESIGN |
| room_program · tenement `width_min` | 4 (from "~4.5–5.5 m frontage") | NEEDS-VERIFY vs RCHME — **now verified, see F2** |
| room_program · tenement `bays`, `entrance` (gable to street) | 2 | DESIGN |
| room_program · `_wealth_tier_note` | tiers | REASONED |
| GroundingGaps #6 firebox opening area 0.44 m² | — | UNSOURCED |
| GroundingGaps #7 chimney stack wall 0.11 m | — | thin / unsourced |
| object_dimensions · `well` 0.9 high, 1.2 dia | — | INFERRED |
| object_dimensions · `hearth` 1.2 × 1.5 × 0.6 | — | INFERRED |
| object_dimensions · `fence_post_rail` height 1.2 | — | INFERRED |

Not in the data at all: **`cottage`** is an alias (`RoomProgram.cpp:153` maps function "cottage" → `croft`);
**`blockhouse`** exists only as the *fixture* era's test typology (`docs/WorldForgeV2BuildLog.md:713`, a re-bayed tavern),
not as a medieval program. Both are treated below as the real-world classes they name.

---

## 1. Sourced dimensions

### 1a. Master table (every number used later carries one of these IDs)

| ID | What | Value as stated → metres | Source | Status |
|---|---|---|---|---|
| D1 | Urban timber-frame bay, York | "bays … usually 10 to 12 ft. long with a depth generally of 16 to 21 ft." → **3.05–3.66 long × 4.9–6.4 deep** | RCHME, *An Inventory of the Historical Monuments in City of York, Vol. 5, Central* (1981), Sectional Preface: Secular Architecture, pp. lviii–xcviii. https://www.british-history.ac.uk/rchme/york/vol5/lviii-xcviii | PR-P |
| D2 | Rental row, deed dimensions (Lady Row, York, 1316) | "a range, 128 ft. long and 18 ft. wide" → **39.0 × 5.5**; "two storeys, eleven bays long; generally each bay formed a single tenement with one room on each floor" → **3.55 per bay** (128 ÷ 11, my division) | RCHME York 5 (1981), monument 222, pp. 143–145. https://www.british-history.ac.uk/rchme/york/vol5/pp135-148 | PR-P |
| D3 | Rental-row unit room | "one room about 10 ft. by 18 ft. on each floor" → **3.0 × 5.5** | RCHME York 5, Sectional Preface (as D1) | PR-P |
| D4 | Chantry priest's house (1316 deed) | "24 ft. by 16 ft." → **7.3 × 4.9** | RCHME York 5, monument 222 (as D2) | PR-P |
| D5 | Town house set gable-to-street | "most often three bays deep, extending 30 to 40 ft. back from the street … sometimes of only two bays and occasionally of four. The upper floors are always jettied on the front elevation" → **9.1–12.2 deep** | RCHME York 5, Sectional Preface | PR-P |
| D6 | Open-hall and cross-wing roof spans (No. 111 Walmgate, York) | "the hall roof, spanning 19 ft. … the contemporary cross-wing with a span of 15 ft." → **5.8; 4.6** | RCHME York 5, Sectional Preface | PR-P |
| D7 | Stud size and spacing | Lady Row c.1316: studs "about 5½ to 6½ in. broad", "24 to 27 in. apart" → 0.14–0.17 at 0.61–0.69 c/c gap; 15th c.: "5 to 6 in. broad and spaced about 18 to 24 in. apart" → 0.13–0.15 at 0.46–0.61 | RCHME York 5, Sectional Preface | PR-P |
| D8 | Floor joists | 1337: "6 in. broad and 4 in. deep", laid flat → 0.15 × 0.10 | RCHME York 5, Sectional Preface | PR-P |
| D9 | Timber-house window openings (Patrick Pool, York) | ground floor "three-light window was 2 ft. wide and one of four lights was 3 ft. wide"; upper "five-light windows were 4 ft. 6 in. and 4 ft. 10 in. wide with sills 3 ft. 9 in. above the floor" → **0.61 / 0.91 / 1.37–1.47 wide; sill 1.14** | RCHME York 5, Sectional Preface | PR-P |
| D10 | Wall-post height, three-bay Shambles building | "substantial wall-posts, c. 26 ft. high" → **7.9** (ground sill to wall-plate of a multi-storey frame) | RCHME York 5, Shambles entries, pp. 212–220. https://www.british-history.ac.uk/rchme/york/vol5/pp212-220 | PR-P |
| D11 | Guild Hall, Stratford-upon-Avon (15th c.; date not checked by me) | "main range … is of five 14-ft. bays. The lower story … is about 18½ ft. wide: the upper story … is about 22ft. wide, being jettied on both east and west sides" → **21.3 long × 5.6 wide (ground), 6.7 (upper)**; wing "two 11-ft. bays and about 16 ft. wide" → 6.7 × 4.9 | VCH *A History of the County of Warwick*, Vol. 3 (1945), "The borough of Stratford-upon-Avon: Introduction and architectural description", pp. 221–234. https://www.british-history.ac.uk/vch/warks/vol3/pp221-234 | PR-P |
| D12 | **Jetty projection, derived** from D11 | (22 − 18.5) ÷ 2 = 1.75 ft → **0.53 per side** (my arithmetic; "about" figures) | as D11 | PR-P (derived) |
| D13 | Jetty / recess depth at Bayleaf (Wealden hall house, 1405–30) | "the roof extends two feet outside the recessed front wall" → **0.61** | Weald & Downland Living Museum, "Bayleaf" building page. https://www.wealddown.co.uk/buildings/bayleaf/ | PR-P (museum's own re-erected building) |
| D14 | Jetty, general statement | "overhangs the floor below by as much as 1,200mm (although usually around 400mm)" → **0.4 typical, 1.2 max** | Designing Buildings Wiki, "Jetty" (cites nothing for the figures). https://www.designingbuildings.co.uk/wiki/Jetty | PR-T (unsourced page) |
| D15 | Wealden-type house in Stratford | "a middle hall of one 14-ft. bay, open from ground to roof, flanked by solar and buttery wings … jettied in front. The south wing … about 12 ft. wide" → **hall bay 4.27; wing 3.66 wide** | VCH Warks 3 (as D11) | PR-P |
| D16 | Gable-fronted jettied town house, Stratford | "jettied upper story and gablehead towards the street, about 18 ft. wide … two 12-ft. bays" → **5.5 frontage; 3.66 bays** | VCH Warks 3 (as D11) | PR-P |
| D17 | Bayleaf plan | "The open hall is of two bays"; both end chambers jettied to the front, hall not; arch braces "thirteen feet long" → 3.96; dendro "1405-1430"; solar end early 16th c.; open hearth (position conjectural); brick stack only 1636 | W&D Bayleaf page (as D13) | PR-P (no overall length, width or height on the page) |
| D18 | Wealden hall house, type plan | "usually had four bays with the two central ones forming the main hall open to the roof"; end bays storeyed and jettied; town "half-Wealden" has one jettied end | Wikipedia "Wealden hall house", citing Brunskill 2000 p. 54 and Quiney 1989 | PR-T |
| D19 | Late-medieval village house, excavated footprint (Walderton, W. Sussex, c.1400–1550) | "at least 10 m long and 6.3 m wide and enclosed at least three rooms. The western room contained a hearth … open hall" → **≥10 × 6.3** | F. G. Aldsworth & R. Harris, "A medieval and seventeenth-century house at Walderton, West Sussex, dismantled and re-erected at the Weald and Downland Open Air Museum", *Sussex Archaeological Collections* 120 (1982), 45–92, at p. 54. ADS PDF | PR-P |
| D20 | Peasant longhouses, Hangleton DMV (13th–15th c.) | "two 'longhouses' … measuring approximately 40ft by 20ft, each divided internally into three rooms" → **12.2 × 6.1** | D. Tankard, "Hangleton & Its Medieval Village", on the W&D "Medieval Building from Hangleton" page (reporting Holden, *SAC* 101 (1963); Hurst & Hurst, *SAC* 102 (1964) — not opened). https://www.wealddown.co.uk/?p=16403 | PR-T |
| D21 | Other peasant buildings, Hangleton | "All the other buildings were less than 30ft long, some with two rooms, some with one" → **< 9.1 long**; 12 building groups / 20 buildings; six ovens, two inside living houses | as D20 | PR-T |
| D22 | Hangleton cottage as re-erected (13th c.) | two rooms: "an inner room with a large domed oven and an outer room with a hearth cut into the chalk floor"; flint walls, surviving "to a height of about three feet" → 0.9 (ruin, not eaves); "Above the level of the eaves … everything remains conjectural"; straw thatch is the museum's choice | as D20 | PR-P for the museum's statement; **no length/width on the page** |
| D23 | Dartmoor longhouse, Hound Tor (13th–14th c.) | largest "nearly 19 metres long"; "walls once about 2 metres high"; granite; "simple thatched roofs supported on oak rafters"; opposed doors + cross passage; byre with central drain at lower end; "11 stone buildings … probably organised in three farms" → **≤19 long; eaves ≈ 2.0** | English Heritage, "History of Hound Tor Deserted Medieval Village". https://www.english-heritage.org.uk/visit/places/hound-tor-deserted-medieval-village/history/ | PR-T (EH summary of the Minter excavations) |
| D24 | Toft (house-plot) widths, Wharram Percy | West Row (south) "tofts c 20m wide"; West Row (north) "measuring on average 18.5m wide"; East Row "ranging from 16m to 22m"; one half-width toft → **16–22, typically 18.5–20** | A. Oswald, *Wharram Percy deserted medieval village, North Yorkshire: archaeological investigation and survey*, English Heritage Archaeological Investigation Report AI/19/2004, report pp. 56, 58 (PDF pp. 61, 63) and PDF p. 72. ADS PDF `englishh2-344817_1.pdf` | PR-P |
| D25 | Croft strips behind tofts, Wharram | "Each croft seems to have been sub-divided into two virtually flat cultivation ridges approximately 8m wide" → croft ≈ toft width (2 × 8) | Oswald 2004, PDF p. 64 | PR-P (no croft LENGTH read) |
| D26 | Large rectangular (barn-like) buildings, Wharram | "internal dimensions of 28m long by 7.5m wide"; "24m long by 7m wide" | Oswald 2004, PDF pp. 51–52 | PR-P (manorial barns, NOT peasant houses) |
| D27 | Wharram peasant houses, count | "about 40 peasant houses and their outbuildings"; "three planned rows"; an excavated 14th-c. longhouse had "a 'cruck' timber frame and – unusually – glazed windows" | English Heritage, "Description of Wharram Percy". https://english-heritage.org.uk/visit/places/wharram-percy-deserted-medieval-village/history/description | PR-T (no dimension on the page) |
| D28 | Burgage plot, Stratford-upon-Avon | "burgage plots, each 3½ perches (57¾ feet) wide, with a depth of 12 perches (198 feet)" → **17.6 × 60.4**; surviving units "frontages of about 55 feet" → 16.8 | Stratford-upon-Avon Society, *History of Bridge Street* (PDF p. 2). https://www.stratfordsociety.co.uk/files/History_of_Bridge_Street.pdf | PR-P (local-society study; the 1196 date is SNIP) |
| D29 | Market infill plots ("Middle Row / Shop Row", Stratford, c.1275–1306) | "a piece of land only 8 feet square is mentioned in 1306, and a neighbouring plot measured only 8½ feet by 10 feet" → **2.4 × 2.4; 2.6 × 3.0** — stalls made permanent | as D28 | PR-P |
| D30 | Burgage plot widths, Scottish burghs | "The peak at 8m … is the largest. This represents the unit plot width"; other peaks "6m, 10m, 12m" (quarter-plot steps of 2 m); candidate units incl. "the perch (5.03m)" → **6 / 8 / 10, modal 8** | H. Tait, "Burgage plot patterns and dimensions in four Scottish burghs", *PSAS* 138 (the page footer reads 2008; the data cites 2009), pp. 225–226, 232. https://journals.socantscot.org/index.php/psas/article/view/9724 | PR-P (already cited in the data; re-read) |
| D31 | Plot-strip depth, New Winchelsea (1280s) | Homan: "'a strip of ground five poles wide was measured off along the south side and along the north side of each [street] block'" → **≈ 25.1 deep** (5 × 16.5 ft, my conversion; the text itself says the perch used may have been 16¼, 16½ or 20 ft → 24.8–30.5) | K. Lilley, C. Lloyd & S. Trick, *Mapping the Medieval Townscape: a digital atlas of the new towns of Edward I* (ADS 2005), Winchelsea "design and plan" discussion, PDF p. 8, n. 486. ADS PDF `winchelsea_design_plan.pdf` | PR-P (the atlas quoting Homan) |
| D32 | Survey unit | "regional variations in the perch meant that it could range between 15 feet and 22 feet … a statute measure of 16½ feet"; at Winchelsea "the common occurrence of a 20-foot measure" → 4.6–6.7; 5.03; 6.1 | as D31, PDF pp. 7–8 | PR-P |
| D33 | Town wall, masonry (York) | "generally 6 ft to 9 ft. thick and 5 ft to 14 ft. high, excluding a parapet 2 ft. to 3 ft. high and merlons 2½ ft. high and usually 4 ft. wide" → **1.8–2.7 thick; 1.5–4.3 high to wall-walk; parapet 0.6–0.9; merlons 0.76 high × 1.2 wide** | RCHME, *City of York, Vol. 2, The Defences* (1972), "The Micklegate Area", pp. 90–107. https://www.british-history.ac.uk/rchme/york/vol2/pp90-107 | PR-P |
| D34 | The same wall stands ON an earth rampart | "The rampart is 72 ft. wide, 17 ft. high outside, and 12½ ft. high inside" → **21.9 wide; 5.2 / 3.8 high**; elsewhere "about 15 ft." and "about 16 ft. high" → 4.6–4.9 | RCHME York 2, "The Central Area", pp. 108–138; Micklegate Area | PR-P |
| D35 | Wall before heightening; thin stretch | "a stretch of wall only about 12 ft. high, subsequently heightened" → 3.7; curtain "3 ft. 10 ins. thick" where the wall-walk rides on arches → 1.17; a (rebuilt 1874) wall-walk "only 3 ft. wide" → 0.9 | RCHME York 2, "Features", pp. 34–55; Central Area | PR-P |
| D36 | Main gate passage | "The four main Bars have passages 10 ft to 12 ft. wide" → **3.0–3.7** | RCHME York 2, "Features", pp. 34–55. https://www.british-history.ac.uk/rchme/york/vol2/pp34-55 | PR-P |
| D37 | Gate leaf / portcullis | Walmgate Bar: "Each leaf is 12¾ ft. high, 6 ft. wide, and about 4 ins. thick" → 3.9 × 1.8 × 0.10 (pair = 3.7 opening); Monk Bar portcullis "14 ft. high and 12¼ ft. wide" → **4.3 × 3.7**; postern portcullis "at least 9 ft. high" → 2.7 | RCHME York 2, "Features" | PR-P |
| D38 | Barbican (which Bar is not named in the extract I read) | "projected 47 ft. in front of the Bar, was 26 ft. wide and about 16 ft. high, with walls 3½ ft. thick" → 14.3 × 7.9 × 4.9; 1.07 | RCHME York 2, Central Area | PR-P |
| D39 | Interval-tower spacing | "The average spacing of the towers at York is 230 ft., but the distance between them can vary from 100 ft. to 800 ft. … Caernarvon and Conway (170 ft.), Oxford (200 ft.), and Newcastle (350 ft., but with two or more intermediate turrets)" → **York 70 (30–244); Conwy/Caernarfon 52; Oxford 61; Newcastle 107** | RCHME York 2, "Features" | PR-P |
| D40 | Interval-tower plan sizes | rectangular "17 ft. wide and projecting 7¼ ft." / "19 ft. wide and projecting 3 ft. to 4 ft."; semicircular "13 ft. wide and projecting 6¼ ft.", "17½ ft. wide and projecting 9½ ft.", "14 ft. wide, and projecting 5 ft."; demi-hexagonal "16 ft. wide and projecting 7 ft." → **4.0–5.8 wide, projecting 0.9–2.9**; many open-backed | RCHME York 2, Micklegate + Central Area | PR-P |
| D41 | Large round corner/river tower | "originally 28 ft. in diameter with walls 4 ft. thick and with a spiral staircase in a rounded turret" → **8.5 dia; 1.2 walls** (by context the tower later used as water-company offices, i.e. Lendal Tower — name not confirmed in the extract) | RCHME York 2, Central Area | PR-P |
| D42 | Town ditch | "average dimensions at York were 50 ft. in width and 10 ft. in depth; at Newcastle … 66 ft. wide and 15 ft. deep, and at London … 75 ft. wide and 6 ft. to 8 ft. deep" → 15.2 × 3.0; 20.1 × 4.6; 22.9 × 1.8–2.4 | RCHME York 2, "Features" | PR-P |
| D43 | Newcastle town wall | "approximately 3 kilometres long", "up to 7.6 metres (25 ft) high", "at least 2 metres (6.5 ft) thick"; seventeen towers; six main gates | Wikipedia "Newcastle town wall", citing a National Trail leaflet and a local-history site | PR-T (weak chain) |
| D44 | Conwy town walls | "1.3 km … long", 21 towers, three gatehouses; east side "originally only 3.6 m (12 ft) high in places", later raised | Wikipedia "Conwy town walls", citing Ashbee 2007 pp. 47–57; Creighton & Higham 2005 | PR-T — **no tower diameter or wall thickness on the page** |
| D45 | Southampton town wall | "2 km … with 29 towers and eight gates"; eastern wall "in places it was only 0.76 m thick", vs "a typical thickness in other English town walls of around 1 m" | Wikipedia "Southampton town walls", citing Creighton & Higham p. 155; Hughes p. 134 | PR-T |
| D46 | Free-standing artillery tower (Cow Tower, Norwich, 1398–99) | "11.2 metres (37 ft) across, 14.6 metres (48 ft) high and divided into three storeys"; walls "1.8 metres … thick at the base", flint core faced in brick; stair turret; nine parapet embrasures | Wikipedia "Cow Tower, Norwich", citing Saunders 1985 p. 111; Ayers et al. 1988 | PR-T |
| D47 | Great hall (Stokesay Castle, 1280s) | "54.5 feet (16.6 m) long and 31 feet (9.4 m) wide"; three cruck-like arches; windows "reaching down to the ground"; solar block "two storeys and a cellar", reached "by external steps"; curtain "34 feet (10 m) high measured from the base of the moat"; court "150 feet (46 m) by 125 feet (38 m)"; moat 4.6–7.6 wide | Wikipedia "Stokesay Castle", citing Summerson 2012 pp. 8–9, 16–17; Cordingley 1963 pp. 91–99 | PR-T |
| D48 | Village/small-town smithy, medieval England (excavated) | Godmanchester "9.6m by 4.8m internally, divided into two rooms. The room on the street front was interpreted as the blacksmith's workshop, the room at the back as domestic" (Webster & Cherry 1975); South Witham "internally about 6m by 4.2m" (Mayes 2002, 37); Waltham Abbey "a large three-bay aisled building 15.7 by 10.1m externally, with a 5.8m wide lean-to. One large 2m-wide doorway … for horses and oxen" (Huggins & Huggins 1973, 131) | J. Franklin, "Iron in the time of Anarchy: excavation of a 12th-century village smithy at Cheveley", *Proceedings of the Cambridge Antiquarian Society* (2020 volume; volume number not legible in the header), p. 143 and Table 6. ADS PDF `PCAS2020-Cheveley-indd.pdf` | PR-P for Franklin; the three comparanda are Franklin's citations (not opened) |
| D49 | Cheveley smithy itself (12th–early 13th c., timber, foundation trenches + posts) | Phase 3 "approximately 5m wide north-south and at least 5m long east-west … a maximum length of 20m"; Phase 4 "the maximum length of the building was about 15m and combined, G13/G20 have a north-south width of about 12m"; "serving as both workshop and dwelling" | Franklin 2020, pp. 128–129 | PR-P |
| D50 | Smithy fittings | hearths "often constructed at waist height"; bosh/quench tank, anvil ("possibly a large boulder"), tool rail, work bench by the light; built "at the edge of the village as was the norm" | Franklin 2020, pp. 143–144 | PR-P (no dimensions) |
| D51 | Roof pitches measured on medieval carpentry | "roof pitches had constant angles of 43, 48, 52, 55 and 58 degrees"; "a 48 degree pitch roof was achieved by the rafter length being 3/4 of the span" | P. Reed, "Researching the knowledge of the medieval master carpenter in setting out roofs and buildings", Carpenters' Fellowship, 28 Jan 2023. https://carpentersfellowship.co.uk/2023/01/28/researching-the-knowledge-of-the-medieval-master-carpenter-in-setting-out-roofs-and-buildings | PR-P (practitioner blog, not peer-reviewed; no building list) |
| D52 | Market cross (Chichester, c.1477–1503) | "standing 50 feet tall" → **15.2**; octagonal, eight flying buttresses, central column, Caen stone | Wikipedia "Chichester Cross", citing Berriman 2001 | PR-T |
| D53 | Galleried courtyard inn (New Inn, Gloucester, 1450) | "large three-storey rectangular building … of oak"; "Open jettied galleries look over the courtyard and form the entry to chambers on the upper floors"; street front seven bays; carriage-way entry | Wikipedia "The New Inn, Gloucester", citing Historic England list entry | PR-T — **no dimension** |
| D54 | Market / guild hall over open ground floor | Thaxted Guildhall: ground floor arcaded "to allow markets to be held", first floor and attic each jettied further on three sides, a central oak post; timber "dates to the late 15th century" (dendro). Ledbury Market House (1617 — out of period): "supported by 16 oak posts", seven bays | Wikipedia "Thaxted Guildhall" (citing NHLE 1112905; RCHME Essex 1, 1916); Wikipedia "Ledbury Market Hall" | PR-T — **no dimension** |

SNIP-only (not to be typed; listed so nobody re-searches them blind):

| ID | Claim | Where it surfaced |
|---|---|---|
| S1 | Stratford plots "set, in 1196" at 12 × 3½ perches | Stratford Society, *The Shrieves House* PDF (search summary; PDF downloaded but not read) |
| S2 | Alnwick "28 foot burgage plots" | Alnwick Civic Society, "Rods, poles and perches" (2020) |
| S3 | Burford plots "1½ perches wide and between 22 and 28 perches deep (7.5 x 110-140m)" | VCH Explore panel (ihr-archive.github.io) |
| S4 | Caernarfon 1298 plots "sixty feet wide and eighty feet deep" | Lilley/Lloyd/Trick atlas or a Cambridge chapter (search summary) |
| S5 | York walls "generally standing 4 metres high and measuring 1.8 metres wide" | a tourist guide site — superseded by D33 |
| S6 | Wharram post-manor building "9.0m by 4.5m", "not dissimilar to some of the smaller medieval buildings" | Oswald 2004 (search summary; my text search of the PDF did not hit it — likely an image page) |
| S7 | Lady Row site "10m by 40m" | York HER MYO1458 — a grid extent, consistent with D2 |
| S8 | Harris, *Discovering Timber-Framed Buildings* pp. 55–57 on jetties | cited by Wikipedia "Jettying"; not read |
| S9 | A Welsh longhouse "14.2m long and 4.3m wide" | search summary, building not identified |

### 1b. Per typology

Blank = not looked for; **NO SOURCE** = looked for and not found. "PR-P/PR-T" refer to the ID's status.

**hall_house** — real class: *Wealden hall house* (SE England, c.1380–1530) and the wider *open-hall house*: a central hall open to the roof, storeyed service end (buttery/pantry) beyond a cross-passage, storeyed solar end.

| Dimension | Value | ID |
|---|---|---|
| Plan | 4 bays, 2 central = open hall; ends storeyed + jettied (Wealden: hall wall recessed under one continuous roof) | D17, D18 |
| Footprint | ≥ 10 × 6.3 (Walderton, excavated plan); shipped example Rufford 14.2 × 6.7 already in the data | D19 |
| Bay length | 4.27 (hall bay, Stratford); 3.05–3.66 (York) | D15, D1 |
| Span (depth) | hall 5.8, wing 4.6 (York); wing 3.66 wide (Stratford) | D6, D15 |
| Storeys | hall 1 (open to roof); ends 2 | D17, D18 |
| Storey / eaves / ridge height | **NO SOURCE** (D10's 7.9 m post is an urban 3-storey frame, not a hall house) | — |
| Wall | timber frame, studs 0.13–0.17 broad at 0.46–0.69; infill not measured | D7 |
| Jetty | 0.61 (Bayleaf); 0.53 derived (Stratford Guild Hall); "usually ~0.4" unsourced | D13, D12, D14 |
| Windows | 0.61–0.91 wide ground, 1.37–1.47 upper, sill 1.14 — unglazed mullions; Bayleaf: "double-height windows" to the upper hall bay, shutters "slide vertically" | D9, D17 |
| Door | **NO SOURCE** (size) — opposed pair at the cross-passage is in the data already | — |
| Roof | crown-post, gabled or hipped; pitch 43–58° family | D17, D51 |
| Hearth | open hearth in the hall; no chimney until 16th–17th c. (Bayleaf stack 1636) | D17 |

**croft / cottage** — real class: *one- or two-room peasant cottage* (cot), flint/cob/stone or cruck-framed.

| Dimension | Value | ID |
|---|---|---|
| Plan | 1–2 rooms; outer room with hearth, inner room (Hangleton: with a domed oven) | D21, D22 |
| Footprint | length < 9.1 (Hangleton, all non-longhouse buildings); width **NO SOURCE** (the data's ≤ 6 m cruck figure stands) | D21 |
| Wall | flint (Hangleton), granite (Dartmoor), eaves ≈ 2.0 (Hound Tor) | D22, D23 |
| Storeys | 1 | D21 |
| Roof | thatch on rafters; form and pitch above the eaves are explicitly conjectural in the museum's own reconstruction | D22 |
| Door / window | **NO SOURCE** (size); "narrow shuttered window openings at the top of the walls … no window glass" (Hound Tor) | D23 |
| Toft width | 16–22, typically 18.5–20 | D24 |
| Croft | two ≈ 8 m ridges per croft; length **NO SOURCE** | D25 |

**longhouse** — real class: *medieval longhouse / byre-dwelling* (Dartmoor granite longhouse; Wharram cruck longhouse): people uphill, cross-passage, shippon/byre downhill with a central drain.

| Dimension | Value | ID |
|---|---|---|
| Footprint | ≈ 12.2 × 6.1, three rooms (Hangleton); largest "nearly 19" long (Hound Tor) | D20, D23 |
| Wall | granite rubble, eaves ≈ 2.0 (Hound Tor); thickness **NO SOURCE** | D23 |
| Storeys | 1 | D23 |
| Roof | thatch on oak rafters (Hound Tor); cruck frame (Wharram) | D23, D27 |
| Openings | opposed doors at the passage; high narrow shuttered slits, no glass | D23 |
| Hearth | open, mid living room | D23 |
| Bay length | **NO SOURCE** for a longhouse bay (data: 4, DERIVED) | — |

**tavern / inn** — real class: *galleried courtyard inn* (New Inn, Gloucester; George Inn, Norton St Philip) in towns; the village alehouse is an ordinary house and has no type of its own.

| Dimension | Value | ID |
|---|---|---|
| Plan | ranges round a yard, carriage-way from the street, open jettied galleries giving onto chambers | D53 |
| Storeys | 3 (New Inn) | D53 |
| Any footprint, bay, height | **NO SOURCE** (Pantin 1961 still unread) | — |
| Frame / jetty / window proxies | as hall_house rows | D1, D7, D9, D12–D14 |

**blacksmith** — real class: *smithy / forge*, timber or stone, at the settlement edge, often workshop + dwelling under one roof.

| Dimension | Value | ID |
|---|---|---|
| Footprint | 9.6 × 4.8 internal, two rooms, workshop to the street (Godmanchester); 6 × 4.2 internal (South Witham); 15.7 × 10.1 + 5.8 lean-to, aisled 3 bays (Waltham Abbey — monastic); Cheveley ≈ 5 wide, 5–20 long, later ≤ 15 × 12 incl. dwelling/yard | D48, D49 |
| Door | 2.0 wide for bringing in horses/oxen (Waltham) | D48 |
| Hearth | waist-height, on a stone base; chimney/hood **NO SOURCE** | D50 |
| Storeys, heights, roof | **NO SOURCE** | — |
| Siting | edge of the village | D50 |

**bakery** — real class: *bakehouse* (oven room) with a street shop. Building dimensions **NO SOURCE**. Only the oven context: ovens stood inside living houses or "in separate outshuts" (Hangleton, six ovens) — D21. Plot/frame proxies as the shop row below.

**butcher** — real class: *shambles shop* (open-fronted stall-board shop). Building dimensions **NO SOURCE** beyond the urban frame: bays 3.05–3.66 × 4.9–6.4 (D1), jettied fronts (D5), a Shambles frame with wall-posts c. 7.9 m (D10). Stall-plot scale: 2.4 × 2.4 and 2.6 × 3.0 (D29).

**apothecary / general_store** — real class: *burgage-plot townhouse with a ground-floor shop* (shop to the street, hall/chamber behind and above). No measured shop was page-read.

| Dimension | Value | ID |
|---|---|---|
| Frontage | 5.5 (gable-fronted, Stratford); 3.0–3.7 per bay (York) | D16, D1 |
| Depth | 9.1–12.2 (3 bays, gable to street) | D5 |
| Storeys | 2–3, upper floors always jettied to the front | D5 |
| Shop-front opening, counter/stall-board | **NO SOURCE** (the data's Chester seld 2–2.5 m frontage stays the only figure) | — |
| Plot | 6 / 8 / 10 wide (modal 8); 17.6 × 60.4 (Stratford); strip ≈ 25 deep (Winchelsea) | D30, D28, D31 |

**tenement** — real class: *rental row ("rents")*, e.g. Lady Row, York (1316): a long two-storey jettied range PARALLEL to the street, one bay = one dwelling, one room per floor.

| Dimension | Value | ID |
|---|---|---|
| Range | 39.0 × 5.5, 11 bays, 2 storeys | D2 |
| Unit | ≈ 3.0–3.55 frontage × 5.5 deep, one room per floor | D2, D3 |
| Jetty | to the street ("several of the posts have greatly enlarged heads to support the jetty"); projection not stated | D2 |
| Frame | studs 0.14–0.17 at 0.61–0.69; crown-post roof, c. 0.15 square posts | D7 |
| Storey heights, roof pitch, door | **NO SOURCE** | — |

**town_hall** — real class: *guildhall / moot hall / market house*: hall on the first floor over an open or semi-open ground floor, jettied.

| Dimension | Value | ID |
|---|---|---|
| Footprint | 21.3 × 5.6 ground, 6.7 upper (Stratford Guild Hall, five 14-ft bays) + wing 6.7 × 4.9 | D11 |
| Bay | 4.27; wing 3.35 | D11 |
| Storeys | 2 (Stratford); 2 + jettied attic over an arcaded market floor (Thaxted) | D11, D54 |
| Jetty | ≈ 0.53 each long side (derived) | D12 |
| Windows | lower: five-light diamond-mullion; upper lights "the full width of the half-bay … seven or eight lights" → ≈ 2.1 wide (half of 14 ft, my arithmetic) | D11 |
| Heights, roof pitch | **NO SOURCE** | — |

**manor_hall** — real class: *aisled or stone ground-floor great hall with a solar block* (Stokesay), or a *first-floor hall over an undercroft* (Boothby Pagnell — no dimension retrieved).

| Dimension | Value | ID |
|---|---|---|
| Hall | 16.6 × 9.4 | D47 |
| Solar block | 2 storeys + cellar, external stair | D47 |
| Curtain / court / moat | 10 high from the moat base; 46 × 38; 4.6–7.6 | D47 |
| Hall height, wall thickness, window size | **NO SOURCE** (page states none) | — |

**blockhouse** — real class (period-correct nearest): *free-standing late-medieval gun tower* (Cow Tower, Norwich, 1398–99). True "blockhouses" are c.1480–1550 coastal artillery works — late for the era.

| Dimension | Value | ID |
|---|---|---|
| Plan / size | round, 11.2 diameter, 14.6 high, 3 storeys | D46 |
| Wall | 1.8 at base, flint core, brick faces | D46 |
| Openings | quatrefoil gunports low, nine wide embrasures at the parapet; door size **NO SOURCE** | D46 |

**market square (stalls, well, cross/statue)**

| Dimension | Value | ID |
|---|---|---|
| Market place size (any) | **NO SOURCE** (page-read). Stratford's market was a deliberately very wide street (Bridge Street; width not stated) later infilled by a "Middle Row" | D28 |
| Stall / infill plot | 2.4 × 2.4; 2.6 × 3.0 | D29 |
| Cross | 15.2 tall, octagonal, open arcaded (Chichester, c.1500 — a city cross; village crosses are a shaft on steps, **NO SOURCE** for their size) | D52 |
| Well-head | **NO SOURCE** | — |
| Statue | not a medieval market fixture — the data already labels it DESIGN | — |

**town wall, gates, towers**

| Dimension | Value | ID |
|---|---|---|
| Wall height (masonry to wall-walk) | 1.5–4.3 on a 3.8–5.2 rampart (York); 3.7 before heightening; 3.6 in places (Conwy, original); "up to 7.6" (Newcastle) | D33, D34, D35, D44, D43 |
| Parapet / merlon | 0.6–0.9 high; merlons 0.76 × 1.2 | D33 |
| Thickness | 1.8–2.7 (York); 1.17 thin stretch; ≥ 2 (Newcastle); 0.76–1 (Southampton) | D33, D35, D43, D45 |
| Wall-walk width | "relatively wide" (Conwy, no number); 0.9 on a rebuilt stretch | D44, D35 |
| Gate passage | 3.0–3.7 wide; leaf 3.9 high; portcullis 4.3 × 3.7 | D36, D37 |
| Gatehouse height / plan | **NO SOURCE** as a number (Monk Bar "four storeys" is SNIP) | — |
| Barbican | 14.3 × 7.9 × 4.9, walls 1.07 | D38 |
| Interval towers | 4.0–5.8 wide, projecting 0.9–2.9, often open-backed; one round tower 8.5 dia., walls 1.2; a free-standing one 11.2 dia. | D40, D41, D46 |
| Tower spacing | 52 (Conwy/Caernarfon), 61 (Oxford), 70 avg (York, 30–244), 107 (Newcastle + turrets) | D39 |
| Ditch | 15.2 × 3.0 (York), 20.1 × 4.6 (Newcastle) | D42 |

**plots, streets, lanes, fences**

| Dimension | Value | ID |
|---|---|---|
| Village toft width | 16–22 (18.5–20 typical) | D24 |
| Village toft depth | **NO SOURCE** (Oswald's figures are on plan sheets, not in the text I could search) | — |
| Burgage width | 6 / 8 / 10 (modal 8, Scotland); 17.6 (Stratford, full plot); 16.8 surviving | D30, D28 |
| Burgage depth | 60.4 (Stratford); ≈ 25 strip (Winchelsea) | D28, D31 |
| Main street width | **NO SOURCE** | — |
| Lane / back-lane width | **NO SOURCE** | — |
| Fence / hedge / toft bank | **NO SOURCE** (Oswald: croft tails end on "a continuous scarp up to 0.7m high, which presumably carried a hedge or fence" — a bank height, not a fence) | D24 src |

---

## 2. Reference images for image-to-3D

All are Wikimedia Commons file pages; licence and author are what the Commons API returned on 2026-11-21.
**None was opened or viewed** — pick by eye. CC BY-SA requires attribution and share-alike of derivatives; a voxelized
reference model kept in-house is the lowest-risk use, a shipped asset derived from one needs the credit line.
Dates and attributions in the last column that carry no section-1 ID come from the Commons description or from my general
knowledge and were **not verified this pass**. "Re-erected" = a real building moved to a museum (good evidence). "Reconstruction" = built new from excavated footings
(walls evidenced to ~1 m, everything above is the museum's conjecture).

| Typology | Commons file page | px | Licence · author | Shows / why / flags |
|---|---|---|---|---|
| hall_house | https://commons.wikimedia.org/wiki/File:Bayleaf_farmhouse_-_geograph.org.uk_-_2565922.jpg | 1600x1200 | CC BY-SA 2.0 · Paul Gillett | Bayleaf, Wealden hall house 1405–30, RE-ERECTED (W&D). Free-standing in a field, so all four sides are photographable; the measured-jetty building (D13). |
| hall_house | https://commons.wikimedia.org/wiki/File:'Bayleaf'_Wealden_Hall_House_at_Weald_and_Downland_Museum,_Singleton,_West_Sussex_-_geograph.org.uk_-_943487.jpg | 640x480 | CC BY-SA 2.0 · Oast House Archive | Same building, second angle (low resolution). |
| hall_house | https://commons.wikimedia.org/wiki/File:Bayleaf_wealden_house_-_geograph.org.uk_-_814311.jpg | 640x480 | CC BY-SA 2.0 · Keith Edkins | Same building, third angle (low resolution) — three angles of one building suit multi-image-to-3D. |
| hall_house | https://commons.wikimedia.org/wiki/File:Wealden_Hall_House,_The_Clergy_House,_The_Tye,_Alfriston,_East_Sussex_-_geograph.org.uk_-_1036876.jpg | 640x480 | CC BY-SA 2.0 · Oast House Archive | Alfriston Clergy House, thatched Wealden, standing on its own plot (in situ; description dates it 'around 1530' — the usual date is 14th c., unverified). |
| hall_house | https://commons.wikimedia.org/wiki/File:Boarhunt_Hall_-_geograph.org.uk_-_814258.jpg | 640x480 | CC BY-SA 2.0 · Keith Edkins | 14th-c. small open hall from Boarhunt, base-cruck truss, RE-ERECTED (W&D): the low-status end of the class. |
| hall_house | https://commons.wikimedia.org/wiki/File:Reconstruction_of_medieval_house_from_North_Cray_(1)_-_geograph.org.uk_-_6703946.jpg | 6012x3486 | CC BY-SA 2.0 · Peter Shimmon | 15th-c. hall house from North Cray, RE-ERECTED (W&D; the uploader's word 'reconstruction' = re-erection). Very high resolution; files (3)(4)(5) of the same set give more angles. |
| croft / cottage | https://commons.wikimedia.org/wiki/File:Mediaeval_Cottage_at_Weald_and_Downland_Museum,_Singleton,_West_Sussex_-_geograph.org.uk_-_943135.jpg | 640x480 | CC BY-SA 2.0 · Oast House Archive | Hangleton flint cottage (D22). **RECONSTRUCTION**: walls from excavated footings, everything above the eaves is the museum's conjecture — massing reference only, not evidence for height or roof. |
| croft / cottage | https://commons.wikimedia.org/wiki/File:Stang_End_Cruck_Cottage,_Ryedale_Folk_Museum,_Main_Street,_Hutton-le-Hole_-_geograph.org.uk_-_7731848.jpg | 2068x1413 | CC BY-SA 2.0 · Jo and Steve Turner | Cruck-framed thatched cottage, RE-ERECTED (Ryedale). Northern cruck form; the standing fabric is largely post-medieval — form reference. |
| croft / cottage | https://commons.wikimedia.org/wiki/File:RyedaleFolkMuseumH3b_-_stang_end,_long_house.jpg | 4592x2584 | CC0 · Helge Klaus Rieder | Same building, CC0 (no attribution duty), high resolution. |
| croft / cottage | https://commons.wikimedia.org/wiki/File:Cruck-frame,_Ryedale_Folk_Museum,_Hutton-le-Hole_-_geograph.org.uk_-_244444.jpg | 480x640 | CC BY-SA 2.0 · Humphrey Bolton | Exposed cruck pair at the gable — shows the frame section the massing hides. |
| croft / cottage | https://commons.wikimedia.org/wiki/File:Poplar_Cottage,_Singleton_-_geograph.org.uk_-_2566109.jpg | 1600x1200 | CC BY-SA 2.0 · Paul Gillett | Poplar Cottage, RE-ERECTED (W&D). **Mid 17th c., out of period** — a clean single small timber cottage with a hipped thatch roof; use for thatch form only. |
| longhouse | https://commons.wikimedia.org/wiki/File:Higher_Uppacott_Farm_-_geograph.org.uk_-_7490012.jpg | 6000x4000 | CC BY-SA 2.0 · Roger A Smith | Higher Uppacott, standing Dartmoor longhouse (14th-c. origin, Grade I). High resolution; 7490014 and 7490017 are further angles. |
| longhouse | https://commons.wikimedia.org/wiki/File:Dartmoor_longhouse_at_Higher_Uppacott_-_geograph.org.uk_-_2597891.jpg | 640x480 | CC BY-SA 2.0 · David Smith | Same building, whole-range view. |
| longhouse | https://commons.wikimedia.org/wiki/File:South_wall_of_Higher_Uppacott_-_geograph.org.uk_-_2597907.jpg | 800x600 | CC BY-SA 2.0 · David Smith | Long-wall elevation — the nearest thing to an elevation drawing found. |
| longhouse | https://commons.wikimedia.org/wiki/File:Hound_Tor_Medieval_Village_(4175).jpg | 4288x2848 | CC BY-SA 4.0 · Nilfanion | Excavated longhouse FOOTINGS at Hound Tor (D23): a real plan at ground level, no superstructure. Use as plan evidence, not as a 3D input. |
| longhouse | https://commons.wikimedia.org/wiki/File:Cilewent_farmhouse,_St_Fagans.jpg | 4877x3230 | CC BY-SA 2.0 · Dave Snowden | Cilewent longhouse, RE-ERECTED (St Fagans). Welsh, stone, rebuilt 1734 on a 15th-c. cruck house — longhouse massing, late fabric. |
| longhouse | https://commons.wikimedia.org/wiki/File:Hendre'r-ywydd_Uchaf_Farmhouse_(1).jpg | 1500x1000 | CC BY-SA 4.0 · Sionk | Cruck-framed hall-house with byre, 1508, RE-ERECTED (St Fagans): timber longhouse form. (3) is a second angle. |
| tavern / inn | https://commons.wikimedia.org/wiki/File:The_New_Inn,_Gloucester_01.jpg | 4000x3000 | CC BY-SA 4.0 · Simon Burchell | New Inn, Gloucester (1450), galleried courtyard inn (D53). Files 02 and 03 are further views; a courtyard cannot be captured unobstructed from one image. |
| tavern / inn | https://commons.wikimedia.org/wiki/File:Courtyard_of_The_New_Inn,_Northgate_Street_-_geograph.org.uk_-_1013786.jpg | 640x475 | CC BY-SA 2.0 · Pauline Eccles | The galleries from inside the yard. |
| tavern / inn | https://commons.wikimedia.org/wiki/File:NortonStPhilipTheGeorge.jpg | 3452x2254 | CC BY-SA 3.0 · Rodhullandemu | George Inn, Norton St Philip (14th–15th c.): stone ground storey, two jettied timber storeys, a single street block — the better single-building 3D input. |
| tavern / inn | https://commons.wikimedia.org/wiki/File:The_George_Inn,_Norton_St_Philip_-_geograph.org.uk_-_6549090.jpg | 4000x3000 | CC BY-SA 2.0 · Eirian Evans | Same building, second angle; 6549085 a third. |
| blacksmith | https://commons.wikimedia.org/wiki/File:Smithy_from_Southwater,_Sussex_-_geograph.org.uk_-_1155752.jpg | 640x480 | CC BY-SA 2.0 · Chris Gunns | Small timber smithy, RE-ERECTED (W&D). **Mid 19th c., out of period** — single small open-fronted workshop, form only. |
| blacksmith | https://commons.wikimedia.org/wiki/File:Gefail_Llawr-y-glyn_(2008).jpg | 3872x2592 | CC BY 2.0 · Kačka a Ondra | Stone smithy from Llawr-y-glyn, RE-ERECTED (St Fagans). **18th c., out of period**; CC BY (no share-alike). No standing or re-erected MEDIEVAL smithy image was found — the evidence is excavated plans (D48, D49). |
| bakery | (none found) | | | No re-erected medieval bakehouse on Commons under the queries tried. The Hangleton cottage (above) contains the reconstructed domed oven. |
| butcher | https://commons.wikimedia.org/wiki/File:The_Shambles,_York_-_geograph.org.uk_-_2971409.jpg | 1024x768 | CC BY-SA 2.0 · John M Wheatley | The Shambles, York: jettied butchers' row. A street scene (not a single isolated building) — crop one house. |
| butcher | https://commons.wikimedia.org/wiki/File:The_Shambles,_York_-_geograph.org.uk_-_3442619.jpg | 425x640 | CC BY-SA 2.0 · Ian Taylor | Same street, portrait framing of the overhangs (low resolution). |
| apothecary / general_store | https://commons.wikimedia.org/wiki/File:Reconstruction_of_a_medieval_shop_from_Horsham_(1)_-_geograph.org.uk_-_7784484.jpg | 7198x4774 | CC BY-SA 2.0 · Peter Shimmon | 15th-c. shop from Horsham, RE-ERECTED (W&D): a free-standing pair of jettied shops with the arched shop openings. Very high resolution — the best shop reference found. |
| apothecary / general_store | https://commons.wikimedia.org/wiki/File:Reconstruction_of_a_medieval_shop_from_Horsham_(2)_-_geograph.org.uk_-_7786362.jpg | 7198x4755 | CC BY-SA 2.0 · Peter Shimmon | Second angle of the same building. |
| apothecary / general_store | https://commons.wikimedia.org/wiki/File:Avoncroft_Museum_-_Merchants_house_from_Bromsgrove,_Worcs_(15th_C)_-_geograph.org.uk_-_693296.jpg | 640x437 | CC BY-SA 2.0 · Mike Goodwin | 15th-c. town house from Bromsgrove, RE-ERECTED (Avoncroft): hall + jettied cross-wing, free-standing. |
| apothecary / general_store | https://commons.wikimedia.org/wiki/File:The_Merchant's_House_at_Avoncroft_Museum_of_Buildings_-_geograph.org.uk_-_7952623.jpg | 1600x900 | CC BY-SA 2.0 · Sandy Gerrard | Same building, higher resolution. |
| tenement | https://commons.wikimedia.org/wiki/File:Lady_Row.jpg | 3648x2736 | CC BY-SA 3.0 · Kaly99 | Lady Row, Goodramgate, York (1316) — the measured row (D2): oblique view down the jettied range. |
| tenement | https://commons.wikimedia.org/wiki/File:1257710_Lady_Row,_72,_70,_68_Goodramgate_York_20240521_0125.jpg | 7844x5145 | CC BY-SA 4.0 · Tilman2007 | Same row, near-frontal, very high resolution; 0124, 0126, 0127 continue along the range. Ground floor is modern shop fronts and brick — only the jetty and upper wall are period. |
| tenement | https://commons.wikimedia.org/wiki/File:1201209_Abbey_Lawn_Cottages,_41-48_Church_Street_Tewkesbury_20240601_0052_DxO.jpg | 7702x5135 | CC BY-SA 4.0 · Tilman2007 | Abbey Cottages, Tewkesbury (15th-c. rental row with shop fronts, restored): the second surviving row of the class; 0054 and 0055 further angles. |
| town_hall | https://commons.wikimedia.org/wiki/File:Thaxted_Guildhall.JPG | 5616x3744 | CC BY-SA 3.0 · T Christopher | Thaxted Guildhall (D54): three-tier double-jettied hall over an open arcaded market floor, stands free on three sides. |
| town_hall | https://commons.wikimedia.org/wiki/File:Guildhall_Thaxted_Essex_England.jpg | 5282x3521 | CC BY-SA 4.0 · Acabashi | Same building, second angle. |
| town_hall | https://commons.wikimedia.org/wiki/File:Titchfield_Market_Hall_(1)_-_geograph.org.uk_-_3120451.jpg | 1800x1166 | CC BY-SA 2.0 · Peter Shimmon | Market hall from Titchfield, RE-ERECTED (W&D), fully free-standing: council chamber over an open arcade. **Built 1620 — out of period**, the cleanest isolated example of the form. (2) is a second angle. |
| town_hall | https://commons.wikimedia.org/wiki/File:Lavenham_Guildhall_-_geograph.org.uk_-_4134571.jpg | 4000x3000 | CC BY-SA 2.0 · Marathon | Guildhall of Corpus Christi, Lavenham (c.1530): close-studded jettied guildhall, corner site. |
| manor_hall | https://commons.wikimedia.org/wiki/File:Stokesay_Castle_Great_Hall_1_-_geograph.org.uk_-_5738738.jpg | 7360x4912 | CC BY-SA 2.0 · Michael Garlick | Stokesay great hall (D47), very high resolution (interior or exterior not known from the title — check). |
| manor_hall | https://commons.wikimedia.org/wiki/File:Stokesay_Castle_Great_Hall_and_Stokesay_Church_-_geograph.org.uk_-_2138579.jpg | 640x431 | CC BY-SA 2.0 · Ian Capper | Exterior of the hall range with its gabled windows. |
| manor_hall | https://commons.wikimedia.org/wiki/File:Norman_Manor_Boothby_Pagnell-Geograph-2142341-by-Stefan-Czapski.jpg | 1024x768 | CC BY-SA 2.0 · Stefan Czapski | Boothby Pagnell manor house (c.1200): first-floor hall over an undercroft with external stair — a compact isolated stone block. |
| manor_hall | https://commons.wikimedia.org/wiki/File:Norman_Manor_House,_Boothby_Pagnell,_aerial_2020_(1)_-_geograph.org.uk_-_6421301.jpg | 800x533 | CC BY-SA 2.0 · Simon Tomson | Aerial oblique of the same building: roof form and footprint; (2) and (3) are further aerial angles. |
| blockhouse | https://commons.wikimedia.org/wiki/File:Torre_Cow,_Norwich,_Inglaterra,_2022-11-19,_DD_48.jpg | 5048x7572 | CC BY-SA 4.0 · Diego Delso | Cow Tower, Norwich (D46): isolated round gun tower on open ground, very high resolution. |
| blockhouse | https://commons.wikimedia.org/wiki/File:Cow_Tower_-_Norwich.jpg | 3000x3828 | CC BY 2.0 · Jim Linwood | Second angle; CC BY (no share-alike). |
| blockhouse | https://commons.wikimedia.org/wiki/File:The_Cow_Tower,_Norwich_-_geograph.org.uk_-_3637963.jpg | 3240x4320 | CC BY-SA 2.0 · Ben Keating | Third angle. |
| market cross | https://commons.wikimedia.org/wiki/File:Chichester_Market_Cross,_Chichester_(NHLE_Code_1026826)_(March_2012)_(2).jpg | 1400x2200 | CC0 · User:Hassocks5489 | Chichester Cross (D52), CC0; files (3) and (4) of the same set are further sides. |
| market cross | https://commons.wikimedia.org/wiki/File:Malmesbury.market.cross.arp.jpg | 1500x2003 | Public domain · Arpingstone | Malmesbury market cross (c.1490), octagonal vaulted shelter; public domain. |
| market cross | https://commons.wikimedia.org/wiki/File:Poultry_Cross,_Salisbury_-_geograph.org.uk_-_5235224.jpg | 1600x1282 | CC BY-SA 2.0 · Des Blenkinsopp | Poultry Cross, Salisbury (15th c.; the flying buttresses are 1852–4 additions). |
| market well / stalls / fences | https://commons.wikimedia.org/wiki/File:Well_near_Winkhurst_Kitchen_-_geograph.org.uk_-_2566021.jpg | 1199x1600 | CC BY-SA 2.0 · Paul Gillett | A museum well-head beside the Winkhurst Tudor kitchen (W&D). **Museum display, date and evidence unknown** — not evidence for a medieval well. No stall or hurdle-fence image was found. |
| town wall | https://commons.wikimedia.org/wiki/File:Conwy_town_walls_6.jpg | 5184x3456 | CC BY-SA 4.0 · Mike Peel | Conwy town wall with open-backed D-towers (1283–87); 5 and 7 are further views. |
| town wall | https://commons.wikimedia.org/wiki/File:Conwy_Town_Walls_-_geograph.org.uk_-_1723348.jpg | 2816x2112 | CC BY-SA 2.0 · David Dixon | South-western stretch: wall, wall-walk and a run of towers in one frame. |
| town wall | https://commons.wikimedia.org/wiki/File:Wall_near_Monk_Bar_-_geograph.org.uk_-_677008.jpg | 640x632 | CC BY-SA 2.0 · Matthew Hatton | York: inner face with the wall-walk carried on arches (the thin stretch of D35). |
| town gate | https://commons.wikimedia.org/wiki/File:Monk_Bar,_York_-_geograph.org.uk_-_7177732.jpg | 1704x2272 | CC BY-SA 2.0 · Philip Pankhurst | Monk Bar, York (portcullis measured, D37); 7177749 is the other face. |
| town gate | https://commons.wikimedia.org/wiki/File:Monk_Bar,_York_YORAG-64.jpg | 3846x4810 | Public domain · William Etty | Monk Bar from outside the walls, by its description showing the barbican that no longer stands. **A 19th-century painting** — good for the lost barbican's form, not for dimensions. |
| town gate | https://commons.wikimedia.org/wiki/File:Walmgate_Bar_Flickr_28_June_2021.jpg | 3648x4997 | CC BY 2.0 · Tim Green | Walmgate Bar, the one York bar that keeps its barbican and timber gates (D37). |
| town gate | https://commons.wikimedia.org/wiki/File:Porte_Walmgate_York_15.jpg | 5184x3456 | CC BY-SA 4.0 · Chabe01 | Walmgate Bar, landscape framing with the barbican in profile. |

Not on Commons, found in passing (SNIP — not opened): a 3D model titled "Bayleaf" on Sketchfab
(https://sketchfab.com/3d-models/bayleaf-44e7e1335e04469a8a3a90d475412668) — licence, author and accuracy unknown.

Measured drawings (plans / elevations / sections) with a clear licence: **none found.** The measured drawings that exist are
inside the reports cited in section 1 and are NOT openly licensed for reuse as far as I could tell — usable for reading
dimensions, not for redistribution: RCHME York 5 Fig. 86 (Lady Row plan/section) and Plate 117; Aldsworth & Harris 1982
figures (Walderton frame); Oswald 2004 Figs 9, 17, 21 (Wharram toft and building plans); Franklin 2020 Fig. 2 (Cheveley smithy plan).

Artists' reconstruction drawings (e.g. Historic England's "Peasant's House, Wharram Percy" education image — page returned 403)
are illustrations, not evidence for any dimension, and are deliberately not listed.

---

## 3. Text-to-3D prompts

Each is consistent with the IDs cited; numbers not in section 1 are deliberately left out of the prompt.

| Typology | Prompt | Based on |
|---|---|---|
| hall_house | "Single isolated early 15th-century Wealden hall house, Kent, England. Rectangular plan about 14 m long by 6.5 m deep, four bays. One continuous steep hipped roof of plain clay tiles, pitch about 50 degrees. The two end bays are two-storey and jetty forward about 0.6 m at first-floor level; the two-bay central hall has no upper floor, so its front wall is recessed under the same eaves, with curved braces up to the flying wall-plate. Exposed oak close-studded frame, pale daub panels, low stone plinth. Tall unglazed mullioned hall window, small shuttered windows elsewhere, one plank door at the cross-passage beside the hall. No chimney. Plain flat ground, no people, no vegetation, neutral daylight." | D13, D17, D18, D19, D51 (length from the data's Rufford cite) |
| croft / cottage | "Single isolated 13th-century English peasant cottage. Small rectangular single-storey building under 9 m long and about 5 m wide, two rooms. Walls of roughly coursed flint rubble about 2 m high, rounded corners. Steep thatched roof of straw, hipped at both ends, thick eaves, no chimney, a smoke-blackened ridge. One low plank door in the long wall, one or two very small shuttered unglazed window slits high in the wall. Plain flat ground, no people, no fence, neutral daylight." | D21, D22, D23 (width is the data's ≤ 6 m cruck cite) |
| longhouse | "Single isolated 13th-century Dartmoor longhouse, Devon. Long low single-storey building about 16 m long by 6 m wide, built down a gentle slope. Thick granite rubble walls about 2 m high. One continuous steep thatched roof, gabled ends, no chimney. Two opposed plank doors mid-length forming a cross passage; the upper half is the dwelling with tiny shuttered slits, the lower half is a cattle byre with a drain hole in the end wall. Plain ground, no people, no animals, neutral daylight." | D20, D23 |
| tavern / inn | "Single isolated mid 15th-century English timber-framed courtyard inn. Ranges on four sides of a small rectangular yard, three storeys, entered by one carriage archway through the street range. Exposed oak frame with close studding and plaster panels, each upper storey jettied over the one below. Inside the yard, open timber galleries at first and second floor with balustrades and external stairs. Steep clay-tile roofs, gabled. Small mullioned windows. Plain ground, no people, no signage, neutral daylight." | D53, D5, D12 |
| blacksmith | "Single isolated medieval English village smithy, 13th century. Single-storey rectangular timber-framed building about 10 m long by 5 m wide, two rooms: an open-fronted workshop at one end with a wide 2 m doorway, a dwelling room behind. Earth-fast oak posts, wattle-and-daub panels, low stone footing. Steep thatched gable roof with a smoke opening over the forge end. Inside the open end a waist-high stone forge hearth, a stone anvil block and a water trough. Plain ground, no people, no horses, neutral daylight." | D48, D49, D50 |
| bakery | "Single isolated medieval English town bakehouse and shop, 14th century. Two-storey timber-framed building, gable to the street, about 5.5 m wide and 10 m deep, upper storey jettied about 0.5 m over a ground-floor shop front with a hinged stall-board under an unglazed opening. At the rear a single-storey stone bakehouse outshut containing a domed masonry bread oven with a short stone flue. Oak frame, daub panels, clay-tile roof. Plain ground, no people, neutral daylight." | D5, D12, D16, D21 (oven outshut) — building size is the shop-house proxy, not a measured bakehouse |
| butcher | "Single isolated late-medieval English butcher's shop of the York Shambles type. Narrow three-storey timber-framed house, one bay about 3.5 m wide and 6 m deep, each upper storey jettied further over the street. Ground floor open-fronted with a broad wooden stall-board shelf under the opening and a pentice roof with iron meat hooks along its edge. Close-studded oak frame, plaster panels, steep tiled gable roof. Plain ground, no people, no meat, neutral daylight." | D1, D5, D10 |
| apothecary / general_store | "Single isolated 15th-century English burgage townhouse with a ground-floor shop. Two storeys, gable to the street, 5.5 m wide by 10 m deep in three bays. Upper storey jettied about 0.5 m on curved brackets. Ground floor: a door at one side and a wide unglazed shop opening with two arched heads and a fold-down counter shutter. First floor: a five-light mullioned window about 1.4 m wide. Oak frame, plaster panels, steep clay-tile roof. Plain ground, no people, neutral daylight." | D5, D9, D12, D16 |
| tenement | "Single isolated early 14th-century English rental row (Lady Row, York type). One long low two-storey timber-framed range 39 m long and 5.5 m deep, parallel to the street, eleven equal bays, each bay a separate dwelling with its own plank door and one small window per floor. The whole upper storey jetties forward on the long street side. Widely spaced oak studs with plaster panels, one continuous steep roof, crown-post trusses, tiled. No chimneys. Plain ground, no people, neutral daylight." | D2, D3, D7 |
| town_hall | "Single isolated 15th-century English timber-framed guildhall. Two storeys, 21 m long by 5.6 m wide at ground level in five bays, the upper floor 6.7 m wide and jettied about 0.5 m along both long sides. Ground storey with rows of unglazed diamond-mullion windows; upper hall with long bands of mullioned lights filling each half-bay. Close-studded oak frame, plaster panels, steep tiled gable roof. Plain ground, no people, neutral daylight." (Variant for a market house: "ground floor open on oak posts as a market arcade".) | D11, D12, D54 |
| manor_hall | "Single isolated late 13th-century English stone manor hall (Stokesay type). A tall single-storey great hall 16.6 m long by 9.4 m wide, rubble sandstone walls, three tall gabled two-light windows reaching nearly to the ground on the long side, one arched door at the low end. Steep stone-slate gable roof. Attached at one end a two-storey stone solar block over a cellar, reached by an external stone stair. No battlements. Plain ground, no people, neutral daylight." | D47 |
| blockhouse | "Single isolated late 14th-century English artillery tower (Cow Tower, Norwich). Circular tower 11.2 m in diameter and 14.6 m high, three storeys, walls 1.8 m thick at the base, slightly tapering. Red brick facing over flint, stone dressings. Small quatrefoil gunports and cross-slits low down, a parapet with nine wide embrasures, a projecting round stair turret rising above the parapet. One plain ground-floor door. Plain ground, no people, neutral daylight." | D46 |
| market cross | "Single isolated late-medieval English market cross (Chichester type). Octagonal open stone pavilion 15 m tall, a pointed arch on each of the eight faces, a central column, eight flying buttresses rising to a crocketed lantern and finial, pale limestone. Plain paved ground, no people." | D52 |
| market stall | "Single isolated medieval market stall: a timber trestle board about 2.4 m long under a lean-to canvas awning on four posts, on a plot 2.4 m square. Plain ground, no goods, no people." | D29 (plot only; the stall form itself is NO SOURCE) |
| town wall segment + tower | "Single isolated segment of a 13th-century English town wall. Limestone ashlar curtain 2 m thick and 4 m high to the wall-walk, standing on top of a grassed earth rampart 5 m high, with a crenellated parapet 0.9 m high and merlons 1.2 m wide. In the middle one semicircular open-backed interval tower 5 m wide projecting 2.5 m, with two cross arrow-slits and a battered base. Plain ground, no people." | D33, D34, D40 |
| town gate | "Single isolated medieval English city gate (York bar type). Rectangular stone gatehouse of three storeys over a single round-arched passage 3.5 m wide and 4.3 m high with a portcullis groove and a pair of studded timber leaves, round corner bartizans at the top, crenellated parapet, arrow slits. Short lengths of curtain wall on a rampart to each side. Plain ground, no people." | D36, D37 (storey count and overall height are NOT sourced) |

---

## 4. Findings

### 4a. What remains unsourced after this pass

1. **Every vertical dimension of a timber house**: storey height, eaves height, ridge height (only D10, an urban frame's 7.9 m post, and D23, a 2.0 m stone eaves). The Weald & Downland pages give no overall size for Bayleaf.
2. **Street and lane widths** — nothing page-read for any medieval village or town street.
3. **Market place size** — nothing page-read.
4. **Village toft DEPTH and croft length** (widths are now sourced, D24).
5. **Door sizes** for every typology (only gate leaves, D37, and a 2 m smithy doorway, D48).
6. **Wall thickness** of peasant stone/flint/cob walls and of wattle-and-daub panels (D7 is stud breadth, not wall thickness).
7. **Roof pitch by covering** (thatch vs tile) from a measured building — D51 is a practitioner's list without buildings; **thatch eaves overhang**.
8. **Inn / tavern** footprint, bay count, gallery width (Pantin 1961; Historic England list entry 1245714-type texts not read).
9. **Bakehouse, butcher's shop, apothecary** building dimensions; **shop-front opening** sizes (D. Clark, "The shop within?", *Architectural History* 43 (2000) not found online).
10. **Guildhall/market-house** storey heights; Thaxted's plan size.
11. **Town gatehouse** overall plan and height; **Conwy tower diameter** (the data's "Conwy's own scale" for 9 m is not supported by anything I could read).
12. **Well-head**, **fence / hurdle / hedge**, **village cross** sizes.
13. **Hearth / louvre / early chimney** sizes; firebox opening (GroundingGaps #6) and stack wall (#7) stay open.
14. **Building counts per tier** (Roberts & Wrathmell; Beresford) — not attempted.
15. Continental comparanda (Bad Windsheim, Kommern) — not attempted this pass.

### 4b. Where shipped data disagrees with a page-read source (to be CHECKED, not changed here)

| # | Data value | Source value | Source | Note |
|---|---|---|---|---|
| F1 | city `walls.gate_width` = 7 | main gate passages 3.0–3.7 wide; portcullis 3.7 wide | D36, D37 (RCHME York 2) | 7 is about twice a real gate passage — unless the key means the whole gatehouse width (no source either way) |
| F2 | tenement `width_min/max` 4–5, sourced as "unit frontage ~4.5–5.5 m (≈15–18 ft)… NEEDS-VERIFY vs RCHME"; description "gable to the street"; depth 2 bays × 4 = 8 | RCHME: range 128 × 18 ft, 11 bays → unit **frontage ≈ 3.0–3.55**, **depth 5.5**; the range lies ALONG the street | D2, D3 | the 18 ft the data reads as frontage is the range's depth; frontage and depth are swapped, and the row is not gable-on. (The 8 m depth and gable entry are already disclosed DESIGN.) |
| F3 | city `walls.height` = 7, labelled inside an "attested ~6–10 m" band | York masonry 1.5–4.3 to the wall-walk (+0.6–0.9 parapet) standing on a 3.8–5.2 m earth rampart; 3.7 before heightening; Conwy 3.6 in places; Newcastle "up to 7.6" (PR-T) | D33–D35, D43, D44 | 7 m of sheer masonry matches only Newcastle's maximum; the commoner form is a lower wall on a bank. The "~6–10 m" band has no citation |
| F4 | city `walls.tower_size` = 9 ("Conwy's own scale") | York interval towers 4.0–5.8 wide projecting 0.9–2.9; one round tower 8.5; Cow Tower 11.2 (free-standing) | D40, D41, D46 | no Conwy tower dimension was found; 9 fits a corner/river tower, not interval towers. Spacing to check against 52–70 m (D39) |
| F5 | village plots: frontage `from_typology` (building 4–8 + side_gap 1) | Wharram toft widths 16–22, typically 18.5–20 | D24 (Oswald 2004) | village plots are 2–4× narrower than the measured tofts; only the DEPTH compression is disclosed in the data |
| F6 | town `plot.depth` 14–24, city 12–18 (DESIGN, disclosed) | 60.4 (Stratford), ≈ 25 strip (Winchelsea) | D28, D31 | disclosed compression; recorded so the ratio is known (0.25–0.4 of Stratford; ≈ Winchelsea strip at the top of the town range) |
| F7 | `bay_length` = 4 for every timber program (DERIVED from a Germanic longhouse ÷ 4) | urban bays "usually 10 to 12 ft" = 3.05–3.66; guildhall/hall bays 14 ft = 4.27; 12 ft and 11 ft bays also recorded | D1, D11, D15, D16 | 4 is right for a hall/guildhall bay, about 10–30 % long for urban shop/row bays; the derivation's own source is not a bay measurement |
| F8 | blacksmith footprint anchored to the Jamestown (Virginia, 1607) smithy 4.9 × 6.1 | medieval English smithies: 9.6 × 4.8 internal two-room with the workshop to the street; 6 × 4.2; 15.7 × 10.1 | D48, D49 (Franklin 2020) | the shipped 5–6 × 8 two-bay plan AGREES with Godmanchester; the anchor is out of period and region and can be replaced. The second room was domestic, not a "storefront" |
| F9 | `stone_manor` `roof.pitch_deg` = 40 (clay tile 35–45, modern roofing sites) | measured medieval roofs cluster at 43 / 48 / 52 / 55 / 58° | D51 (Reed 2023) | weak source (blog) but 40 sits below its lowest value; `timber_cottage` 50 is inside it |
| F10 | `timber_cottage` `ceiling.humble` = 2.5 (ANTHROPOMETRIC, disclosed) | peasant longhouse walls "about 2 metres high" | D23 (English Heritage) | eaves, not ceiling — but the only period wall height read; a 2.5 m storey under the eaves makes the humble cottage read tall |
| F11 | tavern = one 2-storey block, 4 bays | surviving inn type is a 3-storey galleried courtyard building | D53 | type-level, not a number; already disclosed as DESIGN |
| F12 | market square carries a statue (DESIGN, disclosed) and 12 × 12 / 16 × 16 pads | period centrepiece is a cross (15.2 m tall at Chichester); infill stall plots 2.4–3.0 m | D52, D29 | square size itself stays NO SOURCE |

Agreements worth recording (no action): longhouse 16 × 4–6 vs 12.2 × 6.1 and ≤ 19 (D20, D23); croft ≤ 8 long vs < 9.1 (D21);
hall_house 16 × 6–8 vs ≥ 10 × 6.3 and the 5.8 m span (D19, D6) — the 8 m upper width has no page-read support;
town_hall width 6–7 vs 5.6 / 6.7 (D11); manor_hall 8–17 wide vs 9.4 (D47); windows 1 × 1 vs 0.61–1.47 wide (D9);
`timber_cottage` exterior wall 0.222 vs studs 0.13–0.17 broad (D7 — compatible, different quantity); forge work-top 0.8 vs "waist height" (D50).

### 4c. Files fetched for this pass (not in the repo)

RCHME York 5 and 2 pages and VCH Warwick 3 (HTML), Tait 2008, Franklin 2020, Lilley et al. Winchelsea, Aldsworth & Harris 1982,
Oswald 2004, Stratford Society Bridge Street (PDFs) were read from the session scratch directory by text extraction; nothing was
saved into the repository. Oswald 2004 and Aldsworth & Harris 1982 have plan figures (toft plans, the Walderton frame drawings)
that a text search cannot read — rendering those pages is the cheapest next step for toft depth and for a measured timber-frame section.
