# Era reference dossier — ADOBE (Pueblo and Spanish-colonial New Mexico / the south-west, c. 1600–1850, with Ancestral Pueblo background)

First pass, 2026-11-21. Breadth over depth. Purpose: starting points for a WorldForge era pack
(`docs/WorldForgeV2Plan.md` D9, section 14). Nothing here is typed into engine data yet; every number
must go through the grounding-auditor again before use.

Two building traditions share the material and must be kept apart in a pack: **Pueblo** (stacked communal room
blocks, kivas, roof entry) and **Spanish-colonial** (plaza town, mission church, courtyard house, torreon).

## 0. How to read this file

| Code | Meaning |
|---|---|
| **PR** | I fetched the page/PDF text and read the sentence myself. Primary or specialist source. |
| **PR-T** | Page read by me, but TERTIARY (Wikipedia). A lead; replace before shipping. |
| **PR-OCR** | Read in archive.org or Library of Congress OCR text; wording checked, **page number not resolved**, some digits garbled. Re-check against the page image. |
| **SN** | Search-result snippet only; page not opened. Not usable as a source. |
| **NS** | NO SOURCE found in this pass. Do not use a number. |

Feet are given as in the source; metres are my conversion at 0.3048 m/ft ("conv."). **Spanish-era "feet" (pies)
and varas are not English feet** — the length of the pie is NS in this pass; for the vara the only read
data point is Kessell's own conversion "14 varas, or about 38-1/2 feet", i.e. about 2.75 ft = 0.84 m per vara
(derived). I did **not view any image**.

## 1. Settlement types and layout

### 1a. Spanish-colonial town — the 1573 Ordinances ("Laws of the Indies")

Source for the whole table: "Royal Ordinances Concerning the Laying Out of New Towns", translated by Zelia
Nuttall, *Hispanic American Historical Review* 5(2), 1922, pp. 249–254 (public domain; archive.org `jstor-2506027`,
text read in full). Status **PR**.

| Ord. | Rule | Number |
|---|---|---|
| 110 | Plan laid out "by cord and ruler, beginning with the main square", streets running to gates and principal roads, room left to grow symmetrically | — |
| 112 | Inland towns: plaza in the centre, oblong, "its length being equal to at least one and a half times its width" | ratio >= 1.5 : 1 |
| 113 | Plaza "not smaller than two hundred feet wide and three hundred feet long nor larger than eight hundred feet long and three hundred feet wide. A well proportionated medium size plaza is one six hundred feet long and four hundred feet wide" | min 200 x 300 ft; medium 400 x 600 ft; max 300 x 800 ft **as Nuttall prints it** |
| 114 | Four principal streets leave from the middle of each side; two streets meet at each corner; the corners face the four cardinal points | 4 + 8 = 12 streets |
| 115 | Plaza and the four main streets have arcades (portales); the eight corner streets are not obstructed by them | — |
| 116 | "In cold climates the streets shall be wide; in hot climates narrow"; wide where horses are kept or for defence | no number given |
| 118, 120, 124 | Smaller plazas for churches; church lots take a whole block; inland, the church stands apart from the plaza on raised ground | no block size given |
| 126–127 | Lots round the plaza reserved for church, royal/town house, shops; remaining lots drawn by lottery | no lot size given |
| 129–130 | Common, pastures, and as many farm plots as town lots | — |
| 132–134 | Settlers bring "moulds or planks for making adobes"; houses with courtyards and stockyards "as large as possible", usable as a defence; all structures uniform | — |

Caution: the widely repeated maximum is "800 by 532 feet" (ArchDaily and others; **SN**, and attributed to the
Mundigo & Crouch 1977 translation, which I did not open). Nuttall's 1922 text, which I did read, prints 300 for
the maximum width and lists a Spanish-text erratum on exactly that line. **Treat the maximum width as
unresolved**; the minimum (200 x 300) and the medium (400 x 600) agree in every source seen. Block size, lot
(solar) size and street width are **not in the ordinances as translated — NS**.

### 1b. Other settlement forms

| Type | Layout (sourced) | Size data | Source | Status |
|---|---|---|---|---|
| Fortified plaza village (New Mexico) | "built in a square enclosed by contiguous adobe buildings"; entry by two or three alleys "wide enough to admit only animals and people on foot"; a torreon on the south side; the acequia madre runs through the plaza | no dimensions in the text; **the HABS site plan (sheet 1 of 2) is to scale** | HABS NM-128, Plaza del Cerro, Chimayo, written data p. 2, https://www.loc.gov/pictures/collection/hh/item/nm0035/ | PR |
| Mission at a pueblo (Acoma) | Church at the mesa edge facing east, convento against its north wall, house blocks "a couple of hundred feet" beyond | church 45 x 145 ft; convento about 110 ft square | J. L. Kessell, *The Missions of New Mexico Since 1776* (1980), Acoma chapter, https://www.npshistory.com/publications/kessell/nm-missions/acoma.htm | PR |
| Pueblo (Taos) | Two house clusters (North and South); winding ranges crossed by alleys; flat roofs used as outdoor rooms with ramadas | "between one and five stories"; North House five, South House four | SAH Archipedia, "Taos Pueblo", https://sah-archipedia.org/buildings/NM-01-055-0160 | PR |
| Pueblo (Zuni, 1880s survey) | Dense terraced mass with courts; long unbroken outer walls | five terraces at the highest point; one outer wall "two and three stories high ... entire length of 400 feet" (122 m conv.); garden plots "about 10 feet square" with walls 2.5–3 ft high, 6–8 in thick | V. Mindeleff, "A Study of Pueblo Architecture: Tusayan and Cibola", 8th Annual Report of the Bureau of Ethnology (1891), archive.org `tusayancibola00mindrich` | PR-OCR |
| Pueblo (Santa Fe, 1680–92) | After the revolt the plaza was turned into "a fortified pueblo with three- to four-story housing blocks around two squares with kivas" | — | SAH Archipedia, "Santa Fe Plaza", https://sah-archipedia.org/node/16407 | PR |
| Ancestral Pueblo great house (Chaco) | Planned multi-storey D- or arc-shaped room blocks round a plaza with kivas | complexes average more than 200 rooms, up to 700; "one small kiva ... for roughly every 29 rooms"; a family used 3–4 rooms | Wikipedia "Chaco Culture National Historical Park", "Pueblo Bonito" | PR-T |
| Plaza sizes of real towns (Santa Fe, Taos, Albuquerque), pueblo plaza sizes, street widths | — | **NS** | — | NS |

## 2. Building types

| # | Type | Plan form | Footprint | Bay / module | Storeys | Walls | Eaves / ridge | Roof | Door | Window | Source | Status |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | Pueblo room block | Rectangular rooms ("always ... a parallelogram") joined wall to wall and stacked in set-back terraces | room width limited by the viga: "typically less than 15 feet (4.6 m) wide" | viga span | 1–5 (Taos); 5 terraces (Zuni) | Taos: adobe, "70 centimeters (28 inches) thick at the bottom to approximately 35 centimeters (14 inches) at the top". Hopi/Zuni survey: walls "from 7 to 8 feet high ... 15 to 22 inches" thick | storey 7–8 ft (2.1–2.4 m conv.) | flat earth roof on beams "at intervals of about 2 feet", then poles 1.5–2 in diameter, brush, earth | historically none at ground level: "square holes in the roof" reached by ladders | small; selenite slabs "about 1 inch in thickness" | NPS, "Taos Pueblo: World Heritage Site", https://www.nps.gov/articles/000/taos-pueblo-world-heritage-site.htm (PR); Mindeleff 1891 (PR-OCR); Wikipedia "Viga (architecture)" (PR-T) | mixed |
| 1a | Pueblo house details | Hooded corner fireplace; chimney pots | chimney hood "nearly 4 by 5 feet"; Zuni chimney shaft 4–5 ft above the roof; one door sill "about 18 inches above the floor"; setback per storey "about 15 feet" (snippet) | — | — | — | — | — | — | — | Mindeleff 1891 (PR-OCR); NPS search summary (SN, setback) | PR-OCR / SN |
| 2 | Kiva (Hopi, rectangular, 1880s) | Sunken rectangular room, roof hatch with ladder, raised platform at one end, bench/ledge | "usually about 25 feet long and half as wide" (7.6 x 3.8 m conv.) | roof beams "less than 3 feet apart", hatchway "5 by 7 feet" | 1, below or partly below grade | stone masonry; ledge about 2 ft high and 1 ft wide | ceiling "from 5½ [OCR unclear] to 8 feet" | flat earth roof; ladder projects 10–12 ft above the hatch | roof hatch only | none | Mindeleff 1891 | PR-OCR |
| 2a | Kiva (Ancestral Pueblo, round) | Circular sunken room | small kivas 12–15 ft (4–5 m) across; earlier pit structures 12–20 ft diameter, 3–4 ft deep; great kivas up to 63 ft (19 m) diameter | — | 1 | masonry | NS | NS | roof hatch | — | Wikipedia "Kiva" (small kivas, pit structures), "Chaco Culture National Historical Park" (great kivas) | PR-T |
| 3 | Mission church (pueblo mission, 17th c.) — San Esteban del Rey, Acoma | Single nave "like a huge coffin", twin towers, no transept | 45 x 145 ft outside (13.7 x 44.2 m conv.); about 3,600 sq ft inside; SAH gives 145 x 44 ft, 32 ft high | vigas 40 ft long | 1 (tall) | adobe with stone veneer; thickness: "more than two meters" (snippet only) | inside wall height: 14 varas ≈ 38.5 ft by Domínguez (1776); "The HABS drawings made it less than 30 feet"; towers add 25–30 ft | flat earth roof on corbelled squared vigas, latillas, yucca mats, packed adobe; canales | NS | two windows in the south wall | Kessell 1980 (PR); SAH Archipedia, https://sah-archipedia.org/node/16317 (PR); NPS travel page (PR, vigas); WMF (SN, wall thickness) | PR / SN |
| 3a | Mission church (village, 18th c.) — San José de Gracia, Las Trampas | Single nave with transepts, projecting apse, transverse clerestory over the nave roof, baptistry right of the entrance | nave 100 x 52 ft outside (30.5 x 15.8 m conv.); licence of 1760 specified "30 varas long by 15 wide" | NS | 1 | adobe "as great as six feet in thickness" (HABS); 4–6 ft (Wikipedia) | walls 34 ft (10.4 m conv.) | flat earth roof on corbelled beams; wooden balcony on the facade | panelled main door; size NS | transverse clerestory | HABS written data for LoC record nm0015 (number printed as "HM-61" in the OCR, probably NM-61), PDF pp. 3–4, https://www.loc.gov/pictures/collection/hh/item/nm0015/ (PR; the nave length/width and ceiling heights in feet on p. 4 are illegible in the OCR); Wikipedia "San José de Gracia Church" (PR-T) | PR / PR-T |
| 3b | Mission church — San Francisco de Asís, Ranchos de Taos | Cruciform, massive rear buttresses | "approximately 120 feet (37 m) in length" | NS | 1 | adobe; NS | NS | flat | NS | NS | Wikipedia "San Francisco de Asís Mission Church" | PR-T |
| 3c | Adobe church, general limit | — | — | — | adobe walls "seldom rose over 2 stories" | "massive"; buttresses common | "the maximum height of adobe mission churches in the Southwest was approximately 35 feet" (10.7 m conv.) | — | — | — | NPS Preservation Brief 5, *Preservation of Historic Adobe Buildings* (1978), p. 2, https://www.nps.gov/orgs/1739/upload/preservation-brief-05-adobe.pdf | PR |
| 4 | Convento (friary) | Rooms and cloister walk round a square patio, attached to the church | Acoma: "roughly 110-foot square" (33.5 m conv.); patio "roughly 66 feet on a side" (20.1 m conv.) | NS | 1, part 2 with a roofed balcony (mirador) | adobe | NS | flat | vestibule porch (porteria) | NS | Kessell 1980; SAH Archipedia (Acoma) | PR |
| 5 | Hacienda / casa-patio (Hacienda de los Martínez, Taos, from 1804) | Windowless "cliff-like exterior"; rooms one deep round two courtyards (placitas); rooms open onto the courtyard, few internal doors | thirteen single-file rooms round the front placita; whole house "about ninety-five feet by two hundred feet", 21 rooms (snippet) | room = one viga span | 1 | adobe; "two foot thick" (snippet) | NS | vigas, latillas or split cedar/boards (rajas), grass or rushes, "up to four feet of mud" | zaguán (below) | none outward | SAH Archipedia, "Hacienda de los Martinez", https://sah-archipedia.org/buildings/NM-01-055-0181 (PR); Taos Historic Museums (SN) | PR / SN |
| 6 | Zaguán (gate passage) | Covered double-gate entry into a placita | "approximately eleven feet" wide (3.35 m conv.), wide enough for a wagon | — | 1 | — | NS | — | double gate | — | SAH Archipedia (Martínez) | PR |
| 7 | Torreón (defensive tower) | Round tower at a plaza or ranch | **NS** (one stands inside a house at Plaza del Cerro; HABS NM-215 photographs the Lincoln torreon; HABS NM-197 has a "Torreon Isometric" drawing) | — | NS | stone or adobe | NS | NS | NS | loopholes; NS | HABS NM-128 data p. 2 (existence) | PR (form) / NS (dims) |
| 8 | Horno (outdoor oven) | Free-standing beehive dome of adobe, apart from the house | "about 2-3 feet tall" (0.6–0.9 m conv.); diameter NS | — | — | adobe | — | — | small mouth; NS | smoke vent; NS | UNM, Tony Hillerman Portal, "Horno", https://ehillerman.unm.edu/1646 | PR |
| 9 | Portal (long porch) — Palace of the Governors, Santa Fe | Long single-storey adobe range with a full-length post-and-corbel porch on the plaza | **NS** (portal "supported by six posts with carved corbels" in the NHL nomination, snippet) | post spacing NS | 1 | adobe, "thick" | NS | flat | NS | NS | NHL nomination via search summary; HABS record https://www.loc.gov/pictures/collection/hh/item/nm0109/ | SN / NS |
| 10 | Adobe construction kit (applies to all) | — | brick "typically about 25 by 36 cm (10 by 14 in)" | vigas "spaced on close (2-3 feet or less) centers"; vigas 6–10 in diameter, average 15 ft long (Wikipedia); latias "about 2 inches in diameter" | — | load-bearing, massive; mud mortar; stone, brick or rubble foundations, sometimes "almost non-existent" | low parapets; drains (canales) of hollowed logs through the parapet | "6 or more inches of adobe earth" on twigs or planks, mud coat over all, slight fall to the canales | — | — | NPS Preservation Brief 5, pp. 2–3 (PR); Wikipedia "Adobe", "Viga (architecture)" (PR-T) | PR / PR-T |

**Not found anywhere (all NS):** door and window sizes; parapet height; portal depth and post spacing; torreon
diameter/height; horno diameter; room dimensions in plan for any pueblo or Spanish house; plaza sizes of real
towns; acequia width; wall heights of ordinary houses other than Mindeleff's 7–8 ft.

## 3. Reference images for image-to-3D

### 3a. HABS measured drawings and photographs (Library of Congress) — the best source for this era

Rights statement on the LoC records: "No known restrictions on images made by the U.S. Government; images copied
from other sources may be restricted." Record URL = `https://www.loc.gov/pictures/collection/hh/item/<id>/`;
single sheet = `.../item/<id>.sheet.0000Na/`. I read the catalogue records, **not the drawings**.

| Type | HABS record | Call number | What the catalogue lists | Why useful |
|---|---|---|---|---|
| Pueblo room blocks | `nm0095` Pueblo of Acoma | HABS NM,31-ACOMP,1- | 87 drawing sheets (block-by-block plans/elevations; sheet 86 = "Typical Fireplace and Smoke Hood Found in Pueblo Kitchens, Chimney Pots ..."), 3 site sheets (mesa plan and section), photos | measured plans of a whole pueblo: room sizes, storey heights, doors can be scaled off |
| Pueblo room blocks | `nm0099` Pueblo of Taos, Central Portion | HABS NM,28-TAOP,2- | 8 sheets: 2 plans, partial elevations and sections | measured terraced multi-storey block |
| Pueblo houses | `nm0001`, `nm0005`, `nm0006`, `nm0004` Isleta Pueblo | HABS NM,1-ISLEP,1- to 4- | "Adobe & Sod House", "Adobe House (Recessed Portal)", "Adobe House (Walled Forecourt)", "Row of Houses & Sod Wall" | single small houses — ideal isolated-building references |
| Pueblo house | `nm0383` Paisano House, Laguna Pueblo | HABS NM-232 | 10 items | single house |
| Mission church | `nm0067` San Esteban del Rey, Acoma | HABS NM,31-ACOMP,2- (HABS NM-5) | 34 photos (1934, J. M. Slack); 33 measured drawings per Wikipedia (SN) ; data + supplemental PDFs | plans, elevations, sections of the church and convento |
| Mission church | `nm0015` San José de Gracia, Las Trampas | HABS NM,28-TRAMP,1- | photos (front, side elevations, door, clerestory), data pages | elevation photographs |
| Mission church | `nm0053` San Miguel Church, Santa Fe | HABS NM,25-SANFE,1- | 18 sheets incl. "Floor plan church & convento; front elevation, and south elevation" | plan + elevations on one sheet |
| Mission church | `nm0047` El Santuario, Chimayó | HABS NM,25-CHIM,1- | 32 sheets (many are altar screens) | small village church |
| Mission church | `nm0083` Mission N. S. de Guadalupe, Zuni | HABS NM,16-ZUNIP,2- | 12 sheets, elevation and perspective photos | pueblo mission |
| Fortified plaza | `nm0035` Plaza del Cerro, Chimayó | HABS NM,20-CHIM,1- (HABS NM-128) | sheet 1 site plan, sheet 2 elevations | the settlement-layout reference |
| Hacienda | `nm0022` Pascual Martínez House, Ranchito (= Hacienda de los Martínez) | HABS NM,28-RANCHI,2- | 8 sheets (also on Commons as "Pascual Maritnez House ... (sheet N of 8).tif", Public domain) | courtyard-house plan and elevations |
| Portal range | `nm0109` El Palacio Real de Santa Fe | HABS NM,25-SANFE,2- | 18 items | long portal building |
| Torreón | `nm0287` Torreon, Lincoln (HABS NM-215) photos; `nm0066` Manzano tower photos (1916); `nm0227` HABS NM-197 sheet 2 "First Floor Plan ... Adobe Bricks, and Torreon Isometric" | — | photos from SE and NE; one isometric drawing | only torreon material found |
| Kiva | Aztec Ruins, Great Kiva | HABS NM,23-AZ.V,1-B | perspective photos (on Commons, Public domain) | great kiva, catalogued as "reconstructed" — a **reconstruction** (the year 1934 used below is from memory, not verified) |
| Adobe ranch (California) | `ca1105` Adobe NE of Petaluma; `ca1106` Vallejo Adobe | HABS CAL,49-PET.V,2- / ,1- | "Plan, elevations, section, and details" on one sheet; 32 items | compact one-sheet measured set of a two-storey adobe with galleries |

### 3b. Wikimedia Commons photographs

Commons page URL = `https://commons.wikimedia.org/wiki/` + file title (spaces as underscores). Author/licence
as returned by the Commons API on 2026-11-21.

| Type | File | Author | Licence | Shows | Why useful |
|---|---|---|---|---|---|
| Pueblo block | File:Taos Pueblo 1992.jpg | John Fyfe | CC BY-SA 4.0 | Taos Pueblo | terraced block |
| Pueblo block | File:Detail, Taos Pueblo NM.jpg | John Phelan | CC BY-SA 3.0 | Hlaukwima (South House) | closer view |
| Pueblo block | File:Taos Pueblo 090.JPG (also 091, 094, 097) | Elisa.rolle | CC BY-SA 3.0 | Taos Pueblo, four views | multi-view |
| Pueblo church | File:Ansel Adams - National Archives 79-AA-Q01 restored.jpg | Ansel Adams | Public domain | church at Taos Pueblo, 1942, front | PD frontal elevation |
| Mission church | File:San Esteban del Rey Mission, Acoma Pueblo (Valencia County, New Mexico).jpg | James M. Slack (HABS, 1934) | Public domain | Acoma church | PD |
| Mission church | File:DSC 1314 San Esteban Del Rey Mission Church, Acoma, New Mexico.jpg (also 1427, 1436, 1465) | Hasselblad500CM | CC BY-SA 4.0 | Acoma church, four views | multi-view |
| Mission church | File:Ranchos de Taos church 1.jpg (also 2, 3) | Svobodat | CC BY-SA 3.0 | San Francisco de Asís, three views | the buttressed apse is the classic massing study |
| Mission church | File:San Francisco de Asis Mission Church.JPG | Travis K. Witt (Liveon001) | CC BY-SA 3.0 | exterior | front |
| Mission church | File:San José de Gracia Church, built of adobe in the 1760s in Las Trampas, New Mexico LCCN2011632008.tif | Carol M. Highsmith | Public domain | Las Trampas church | PD, high resolution |
| Mission church | File:Historic San Jose de Gracia Catholic Church Las Trampas NM.JPG | ChuckNugent | CC BY-SA 3.0 | facade, towers and gate | facade with atrium gate |
| Horno | File:A Horno (an adobe oven) at Taos Pueblo in New Mexico in 2003.jpg | Michael J. Brown (Coeus1312) | Public domain | single horno | isolated object, PD |
| Horno | File:Pueblo at Taos, New Mexico. Ovens for cooking food - Arthur Rothstein, 1936 April (LOC).png | Arthur Rothstein (FSA) | listed CC BY 4.0; description says LoC public domain | hornos in front of the pueblo | scale against the house block |
| Great kiva | File:Aztec Ruins National Monument-The Great Kiva.jpg | U.S. National Park Service | Public domain | schematic DIAGRAM of the Great Kiva | the only plan-type drawing found |
| Great kiva | File:Interior Great Kiva Aztec Ruins 1.JPG | Wvbailey | CC BY-SA 3.0 | interior of the rebuilt great kiva | roof structure (**1934 reconstruction**) |
| Hacienda | File:Martinez Hacienda (32824137547).jpg | Bradley Weber | CC BY 2.0 | Martínez hacienda | exterior |
| Portal range | File:Palace of the Governors pano.jpg | Dicklyon | CC BY-SA 4.0 | Palace of the Governors, panorama | full-length portal (note: the present facade is a 1909–13 restoration — from memory, verify) |
| Portal range | File:Palace of the Governors, Santa Fe 1991.jpg | David Taylor | CC BY 2.0 | portal | second view |

Not found on Commons in this pass: torreon, zaguán, a kiva with ladder at a living pueblo, carreta.

## 4. Text-to-3D prompts

| Type | Prompt |
|---|---|
| Pueblo room block | Multi-storey adobe pueblo house block, Taos, New Mexico, 18th century. Stacked flat-roofed mud-plastered rooms stepping back in terraces, round log beam ends (vigas) projecting from the walls, wooden pole ladders between roof levels, very few small openings, soft rounded corners, brown earth colour. Single isolated building, plain ground, no people. |
| Small pueblo house | Single-storey adobe pueblo house, Rio Grande valley, 19th century. Low rectangular flat-roofed mud-brick house with projecting vigas, low parapet, one plank door, one tiny window, a wooden ladder to the roof, a corner chimney pot. Single isolated building, plain ground, no people. |
| Kiva | Pueblo kiva, New Mexico. Low round masonry-and-adobe drum mostly sunk in the ground, flat earth roof with a square hatch, a tall two-pole wooden ladder rising from the hatch. Single isolated structure, plain ground, no people. |
| Mission church | Spanish-colonial adobe mission church, New Mexico, 18th century. Long single-nave church with massive battered mud walls and buttresses, two squat bell towers, wooden balcony over a panelled door, flat roof with wooden water spouts (canales), raised clerestory at the far end, walled forecourt. Single isolated building, plain ground, no people. |
| Convento | Franciscan mission friary (convento), New Mexico, 17th century. Single-storey adobe rooms around a square cloister courtyard with a timber post-and-corbel walk, flat earth roofs, small door openings. Single isolated building, plain ground, no people. |
| Hacienda | Spanish-colonial fortified adobe hacienda, Taos, New Mexico, c. 1800. Large windowless single-storey rectangle of thick mud walls enclosing two courtyards, one wide double-gate wagon entrance, flat roofs with low parapets and projecting vigas. Single isolated building, plain ground, no people. |
| Portal house | Spanish-colonial adobe house with portal, Santa Fe, New Mexico, 18th century. Long low flat-roofed adobe range with a full-length porch of round timber posts carrying carved corbel brackets and a heavy beam, plank doors, earthen floor. Single isolated building, plain ground, no people. |
| Torreón | Spanish-colonial defensive tower (torreón), New Mexico, 18th century. Round two-storey tower of rough stone and adobe, tiny loophole openings, one low door, flat roof with a low parapet. Single isolated structure, plain ground, no people. |
| Horno | Adobe beehive bread oven (horno), New Mexico pueblo. Small dome of smooth mud plaster on a low base, an arched mouth at the front and a small smoke hole near the top. Single isolated object, plain ground, no people. |

## 5. Materials, props and street furniture (draft list)

**Materials named in the sources read:** sun-dried adobe brick with straw; mud mortar and mud plaster renewed
yearly; stone rubble foundations and stone veneer; peeled log vigas (ponderosa pine, aspen, mesquite, cedar);
carved corbels; latias/latillas, split cedar (cedros, rajas), cypress (savinos), saguaro ribs; brush, grass,
rushes, yucca mats; packed earth roofing; hollowed-log canales; selenite window slabs; later fired-brick parapet
copings and sawn-lumber trim (after the railroad — out of period for a colonial pack).

**Prop candidates (from general knowledge unless a source is named; unsized):** pole ladders (sourced: NPS, Mindeleff);
hooded corner fireplace and chimney pots (HABS Acoma sheet 86); built-in adobe benches (bancos) and storage bins
(SAH, Martínez); horno; ramada (shade frame on a roof or plaza, SAH Taos); drying racks with chile strings and
corn; metate and mano (Mindeleff mentions metates); ollas and water jars; acequia ditch with plank footbridge and
headgate; carreta (two-wheel ox cart); hitching rail; well with windlass; wooden cross and walled churchyard
(campo santo); bell on a yoke; cottonwood-log trough; adobe-brick drying yard with wooden moulds (ordinance 132);
corral of upright poles; beehive kilns; loom and wool racks (hacienda workshops); wooden grain chest; hide-covered door.

## 6. What is unsourced, and cost of a full pack

**Unsourced after this pass:**

1. Every opening size (doors, windows, roof hatches except the Hopi kiva hatch).
2. Room plan sizes — only the viga-span limit (tertiary) and the Hopi kiva.
3. Torreón, horno diameter, portal, parapet, zaguán height.
4. Real plaza sizes and street widths; block and lot sizes (the 1573 ordinances give none).
5. The maximum plaza width in the ordinances (300 vs 532 ft) and the metric length of the pie.
6. Mindeleff page numbers; several digits in his text and in the Las Trampas HABS data are OCR-damaged.

**Why this is still the best-supplied era:** the HABS sets above are measured drawings at stated scales, in the
public domain, for every type on the list except the horno — the missing numbers are a matter of reading the
sheets (they are images; nobody has transcribed them here), not of finding a source.

**Work relative to the medieval pack (my judgement from the plan text; I did not read the generator code):**

| Item | Analogue in the existing generator? | Note |
|---|---|---|
| Rectangular single-storey rooms, load-bearing thick walls | Yes | simplest wall system of the three eras |
| Flat earthen roof with vigas, parapet and canales | No | new roof class — but geometrically the easiest possible roof (a slab with a rim); roofs are walkable, which the walkability gate must understand |
| Plaza-and-grid town plan with numeric rules | Partly — the Roman insula grid already exercises the planner's non-organic branch | the ordinances give the plaza numbers directly |
| Stacked, set-back room blocks entered by ladders and roof hatches | No | vertical circulation by ladder on the outside; no interior stairs; "reachable" has to mean climbable |
| Sunken kiva with roof entry | No | below-grade room, hatch + ladder |
| Courtyard house with zaguán | Partly | rooms one deep round a void; Roman domus/insula work may transfer |
| Single-nave church with towers and clerestory | Partly | a landmark program; simpler than a stone church |
| Battered, rounded mud walls | No | cosmetic; needs sub-voxel softening or an accepted square approximation |
| Free-standing oven, torreon, ramada, corral | No, but trivial | props or tiny programs |

Overall: the **smallest** build of the three if the pack starts with the Spanish-colonial plaza village
(rectangular rooms, flat roofs, a numeric plaza rule, a courtyard house, a church); the stacked pueblo with ladder
access is a second, harder increment.
