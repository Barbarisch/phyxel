# Roman era reference - dimensions, reference images, text-to-3D prompts

Research input, NOT a ruling. Web pass 2026-11-21 (second pass; the first is `../m5f_insula_research_2026-11-21.txt`,
the strip-building record is `../m5f_strip_building_grounding.txt`). Nothing here is a number in the tree.
No image was downloaded into the repo; no existing file was edited.

## 0. How to read this file

READ-STATUS codes (every value carries one):

| Code | Meaning |
|---|---|
| PR | page-read: I downloaded the page / PDF text and read the quoted sentence myself |
| PR-f | page-read through the fetch tool: the tool opened the page and returned the quoted French sentence (its quotes are capped at about 125 characters, so re-open the paragraph before a number goes into data) |
| PR-2 | page-read, but the page is a SECONDARY source (Wikipedia) |
| SN | search snippet only - not a source |
| PRIOR | already page-read in one of the two earlier records (not repeated here) |
| NS | NO SOURCE found |
| calc | my arithmetic on a sourced value - label it so if used |

Conversions are mine: English foot 0.3048 m; Roman foot 0.296 m (Stevens' "17.70 metres or 60 Roman feet" gives 0.295);
Oscan foot 0.275 m (from Mau's own "180 English feet (200 Oscan feet)").

Sources opened in this pass (short keys used in the tables):

| Key | Full reference | Route that worked |
|---|---|---|
| OA | J. Th. Bakker (ed.), *Ostia - Topographical Dictionary*, ostia-antica.org, per-building pages (dated 2022-2024 on the pages) | direct HTML |
| SCH | Julien Schoevaert, *Les boutiques d'Ostie. L'economie urbaine au quotidien, Ier s. av. J.-C. - Ve s. ap. J.-C.* (Collection de l'Ecole francaise de Rome 537), Rome 2018. Ch. 5 "Entre les murs des boutiques" pp. 55-78 https://books.openedition.org/efr/33825 ; ch. 6 pp. 79-102 https://books.openedition.org/efr/33830 ; ch. 11 pp. 209-232 https://books.openedition.org/efr/33905 ; online catalogue https://www.hist.uib.no/antikk/dias/ostiashops.pdf | OpenEdition via fetch tool (direct curl hits a bot wall); catalogue PDF read as text |
| STO | Hanna Stoeger, *Rethinking Ostia: a spatial enquiry into the urban society of Rome's imperial port-town* (Archaeological Studies Leiden University 24), Leiden 2011, ch. 5 "Insula IV ii" https://hdl.handle.net/1887/18192 | open-access PDF, text extracted; page numbers below are from the running heads and are approximate |
| MAU | August Mau, tr. F. W. Kelsey, *Pompeii: Its Life and Art*, New York 1899 (public domain) https://archive.org/details/pompeiiitslifear00mauauoft | archive.org full text (djvu OCR); pages from running heads, approximate - verify against the scan |
| GUE | Peter Guest, "The Forum-Basilica at Caerwent (Venta Silurum): A History of the Roman Silures", *Britannia* 53 (2022) 227-267, doi:10.1017/S0068113X21000489 (open access) | PDF text |
| ROC | Michael Rocchio, *Image-based 3D models as tools for critically analyzing the architectural composition of Pompeian intersections*, PhD thesis UCLA 2020, sect. 2 and 3.B.i https://escholarship.org/uc/item/0dg2c4ww | PDF text |
| EH | English Heritage, "Description of Silchester Roman City Walls and Amphitheatre" https://www.english-heritage.org.uk/visit/places/silchester-roman-city-walls-and-amphitheatre/history/description | direct HTML |
| WP | en.wikipedia.org pages named in the row (secondary) | direct HTML |

Still NOT opened (alternative routes tried and failed): Packer 1971 *The Insulae of Imperial Ostia* (not on archive.org; JRS 1967
article paywalled) - only reached through OA / SCH / STO citing him; Stevens 2005 (BABesch 80, 113-123) - no PDF found, reached only
through OA's Garden Houses page; Poehler 2017; Hartnett 2008 (AJA) - only his 2017 book chapter opened, which has one street width;
Hermansen 1982 and Meiggs 1973 (archive.org, borrow-only); DeLaine's medianum papers; Calza 1915 / 1941 PDFs on the park site (fetch failed).

---

## 1. Building types and dimensions

### 1.1 Insula (multi-storey apartment block; Ostia)

What: brick-faced concrete block, shops (tabernae) with mezzanines on the street fronts, flats (cenacula) above reached by
stairs opening directly on the street; often an inner court. Plan forms: (a) courtyard block (Casa di Diana, Caseggiato degli
Aurighi / del Serapide); (b) row block with medianum flats, windows on one long side (Casette-tipo, Garden Houses, Via dei Vigili);
(c) small single-frontage block (Caupona del Pavone).

| # | Element | Value | Building | Source | Status |
|---|---|---|---|---|---|
| I1 | footprint w x d | "an almost rectangular area of about 385 m2 (c. 16 x 24 m)"; "16 metres of street front"; no party walls with its N and S neighbours | Caupona del Pavone, Ostia IV ii 6 (a small commercial-residential block with upper floor) | STO sect. 5.2.6, p. ~116 | PR |
| I2 | footprint area (no w x d) | 228.9 m2 rectangular; 258.1 m2 rectangular; 309.3 m2 rectangular; 535 m2; c. 406 m2 L-shaped; 141.8 m2; 153.6 m2 (taberna row) | Ostia IV ii, buildings 3 (industrial wing), 4, 5, 7, 8, 12, 14 | STO sect. 5.2.3-5.2.14 | PR |
| I3 | whole urban block | "The Insula covers a total area of 7321 m2" (14 buildings) | Ostia Insula IV ii | STO ch. 5 summary | PR |
| I4 | building area | "The total area is roughly 730 square metres" | Caseggiato del Balcone Ligneo, Ostia I,II,2.6 | OA regio1/2/2-2.htm | PR |
| I5 | flat (ground-floor medianum apartment) area | "c. 220 square metres" each, 16 flats in the complex, two central blocks of four back-to-back | Case a Giardino III,IX | OA regio3/9/9.htm | PR |
| I6 | flat area | "approximately 150 square meters" | Case di Via dei Vigili II,III,3-4 | OA regio2/3/3-3.htm | PR |
| I7 | flat area, general | "The average ground floor area is c. 200 square meters" (medianum apartments) | Ostia, general | OA dict/topics/houses/houses-intro.htm | PR |
| I8 | Casa di Diana footprint w x d | NO SOURCE (OA gives the Packer plan image with no stated dimensions; the "23 x 39 m" figure I recalled could not be found on any page) | Casa di Diana I,III,3-4 | - | NS |
| I9 | storeys | "four storeys (c. 17.70 metres or 60 Roman feet)" - inferred from the wall thickness, after Stevens | Case a Giardino | OA regio3/9/9.htm | PR (upgrades yesterday's snippet; it belongs to the Garden Houses) |
| I10 | storeys preserved | "The first floor has been preserved to a height of c. 1.00."; "The facade of the first floor was decorated by a narrow balcony." | Casa di Diana | OA regio1/3/3-3.htm | PR |
| I11 | mean storey height | 17.70 / 4 = 4.43 m per storey incl. floors and roof | Case a Giardino | calc on I9 | calc |
| I12 | ground storey: ceiling | "The ceiling was at a height of 3.90 meters" | Case di Via dei Vigili | OA regio2/3/3-3.htm | PR |
| I13 | ground storey incl. mezzanine: vault | "standing height of c. 6.50 m which marks the top of the vaults, while the spring of the vaulting is at 4.20 m height" | Caseggiato dell'Ercole, Ostia IV ii 2-3 | STO ch. 5 fn 103, p. ~99 | PR |
| I14 | shop + mezzanine section | mezzanine floor "3,4 m au-dessus du pavement"; barrel vault total "6,2 m"; so the mezzanine "d'une hauteur de 2,8 m"; vault springs "1,3 m au-dessus du plancher" | Caseggiato del Larario I,IX,3 | SCH ch. 5 par. 46 | PR-f |
| I15 | storey height ABOVE the first floor | NO SOURCE as a measured value (only I11, a mean) | - | - | NS |
| I16 | wall thickness, ground floor | "0.90 - 1.00, pointing to a considerable number of floors" | Casa di Diana | OA regio1/3/3-3.htm | PR |
| I17 | wall thickness | "The thickness of the walls is 60 cm." | Case a Giardino | OA regio3/9/9.htm | PR |
| I18 | wall thickness | "c. 0.60 m ... suggests that the building had several storeys" | Caseggiato dell'Ercole IV ii 3 | STO p. ~96 | PR |
| I19 | wall thickness, general | upper floors "supported by walls that were often 60 centimeters (two Roman feet) thick" | Ostia, general | OA houses-intro.htm | PR |
| I20 | wall thickness, legal cap | 1.5 Roman ft on a public way (Vitruvius 2.8.17) | Rome | PRIOR | PRIOR |
| I21 | door, room to court | "wide, centrally positioned doors (1.50 m w, 2.40 m h)"; "Smaller door openings (1.15 m w)" | a courtyard building with porticoes and built triclinia (a guild seat; the passage I read does not repeat its name) | STO ch. 8 | PR |
| I22 | corridor | "A long corridor (1) at the west end (w. 1.86)" | Caseggiato IV,IV,6 | OA regio4/4/4-6.htm | PR |
| I23 | window, medianum facade | "three windows (w. 1.50) in the south facade" | Caseggiato IV,IV,6 | OA regio4/4/4-6.htm | PR |
| I24 | window, mezzanine | "une fenetre n'excedant pas un metre de hauteur et 80 centimetres de largeur"; Packer (quoted in fn 42): "Mezzanine windows above shop doors were usually under 1.00 in height and between 0.60 and 0.80 in width" | Ostia shops | SCH ch. 5 par. 47 and fn 42 | PR-f |
| I25 | window, ground floor | "two windows, beginning at 0.20 above the floor, the south-west one preserved to the full height of 1.05" | Casa di Diana, room I12 | OA regio1/3/3-3.htm | PR |
| I26 | window, upper floors (general) | 1.50-2.00 high x 0.90-1.30 wide (Packer) | Ostia | PRIOR (snippet) | SN |
| I27 | stair: tread and riser | "The depth of most treads is c. 0.29, one Roman foot. The height of most risers is c. 0.20." Often only the lowest steps travertine, the rest brick or wood | Ostia, general (after Van der Meer 1999) | OA dict/topics/travertine/travertine01.htm | PR |
| I28 | stair: flight length | "The first ramp consists of 16 treads" (travertine, street stair in the SE corner) -> about 3.2 m rise at 0.20 (calc), which matches the 3.4-3.9 m ground storey | Casa di Diana | OA regio1/3/3-3.htm | PR + calc |
| I29 | stair WIDTH, flats | NO SOURCE | - | - | NS |
| I30 | medianum width / length | NO SOURCE (plan only: Hermansen 1982 fig. 8 on OA; text: rooms on three sides, windows in one long wall, exedra at each end, two cubicula behind) | - | OA houses-intro.htm (layout only) | NS |
| I31 | largest room | "6.75 x 8.75 metres" | Domus di Giove e Ganimede I,IV,2, room 27 | OA regio1/4/4-2.htm | PR |
| I32 | courtyard | "grouped around a long trapezoid courtyard"; inner passage "about 2.5 m wide" | Caseggiato dell'Ercole IV ii | STO p. ~96, fn 153 | PR |
| I33 | courtyard w x d of a courtyard insula (Diana, Aurighi, Serapide) | NO SOURCE | - | - | NS |
| I34 | portico on the street | 65 m long "supported by 12 pillars and paved with bipedales" (-> about 5.4-5.9 m per bay, calc) | Caseggiato dell'Ercole | STO p. ~96 | PR + calc |
| I35 | portico depth | NO SOURCE for Ostia (SCH ch. 11 gives lengths only: mean 62 m; a colonnade "haute de quatre metres") | - | SCH ch. 11 par. 13, 41 | PR-f (length, height) / NS (depth) |
| I36 | roof | NO SOURCE for form or pitch of an Ostian insula roof | - | - | NS |
| I37 | height limits | 70 ft (Augustus, Strabo 5.3.7); 60 ft (disputed) | Rome | PRIOR | PRIOR |
| I38 | Rome comparison | "ground floor with its row of shops ... up to 400 square metres"; "at least five stories" (unsourced on the page); street narrowed "to about 3.8 metres" | Insula dell'Ara Coeli | WP "Insula dell'Ara Coeli", citing Priester 2002 pp. 91-92 | PR-2 |

### 1.2 Taberna (shop) and taberna row

What: one wide-fronted room open to the street, closed by sliding shutters in a grooved threshold; often a back room and a
timber mezzanine (pergula) reached by a short masonry stair base plus a ladder. Rows are a single line of equal-depth rooms behind a
portico or along a facade.

| # | Element | Value | Building | Source | Status |
|---|---|---|---|---|---|
| T1 | depth and width | "si la profondeur de 6,70 m est commune a toutes les boutiques, leur largeur varie de 4,35 a 5,20 m" | Caseggiato dell'Ercole Bambino, Ostia | SCH ch. 5 par. 27 | PR-f |
| T2 | depth and width | "la profondeur fixe d'environ 5,80 m, tandis que leur largeur varie de 3,77 a 5,43 m" | Portico di Nettuno, Ostia | SCH ch. 5 par. 27 | PR-f |
| T3 | floor area, all Ostia | "comprise entre 4,7 et 130,5 m2 ; la surface moyenne est de 35,6 m2", "la surface mediane est de 31,3 m2" | Ostia (whole corpus) | SCH ch. 5 par. 27 | PR-f |
| T4 | floor area, Pompeii | mean 35 m2, "valeurs extremes de 10 et de 150 m2" | Pompeii | SCH ch. 5 par. 27 | PR-f |
| T5 | floor area by period | 24.2 m2 (1st c. BC) -> 50 m2 (later 1st c. AD) -> 28.5-35.6 m2 (2nd - mid 3rd c.); early shops "entre dix et quinze metres carres" | Ostia | SCH ch. 6 par. 4, 25 | PR-f |
| T6 | shop areas in one insula | 58, 50, 34, 29, 30, 31, 33, 32 m2 (8 shops, b14-b21) | Casa di Diana | SCH catalogue, entry "Caseggiato di Diana I, iii, 3-4" | PR (column layout in the PDF text is loose - check the row order) |
| T7 | shop front opening (threshold length) | "comprise entre 116 et 506 cm, pour une moyenne de 293 cm"; "88 % ... entre 200 et 400 cm" | Ostia | SCH ch. 5 par. 7 | PR-f |
| T8 | shop front opening | "wide door openings (ranging from 3.5 to 3.9 m) suitable for commercial interaction"; elsewhere 1.95-2.70 m; 1.20-2.00 m "not quite as wide as often found in tabernae"; 1.40 m read as non-commercial | Ostia IV ii 14, IV ii 8, IV ii 7 | STO sect. 5.2.7-5.2.14 | PR |
| T9 | threshold block | width (front to back) 20-80 cm, mean 45.7; height 11-40 cm, mean 25.5 cm | Ostia | SCH ch. 5 par. 6 | PR-f |
| T10 | door to back room | "entre 90 et 140 cm de largeur" | Ostia | SCH ch. 5 par. 7, 54 | PR-f |
| T11 | back room | 10.5-12.5 m2; about 12 m2 | Portico a tetto spiovente; IV,VIII,5 | SCH ch. 5 par. 53 | PR-f |
| T12 | mezzanine floor level | 3.4 m (I14); "usually ... three-and-a-half to four metres" (OA shops intro) | Ostia | SCH par. 46; PRIOR | PR-f / PRIOR |
| T13 | mezzanine stair | masonry base "entre un metre et un metre soixante-dix de hauteur", "trois a cinq marches"; ladder above, width "77 cm"; floor opening 2.72 m x 0.77 m | Ostia; Larario | SCH ch. 5 par. 37, 39, 40 | PR-f |
| T14 | shop height, Pompeii | "The shops with their upper floors, pergulae, were nineteen feet high" (5.8 m); Caecilius Jucundus "nearly 16 feet" (4.9 m); "upper floor, pergula, about 12 feet" [above the floor] (3.7 m) | House of the Faun; House of Caecilius Jucundus | MAU ch. "The Pompeian House", pp. ~270, ~283-284 | PR |
| T15 | row length | porticoes with shops: 10 to 260 m, "pour une moyenne de 62 m" | Ostia | SCH ch. 11 par. 13 | PR-f |
| T16 | encroachment in front of a portico | 1.67 m and 3.42 m | Portico a tetto spiovente; Portico di Nettuno | SCH ch. 11 par. 52 | PR-f |
| T17 | shop wall thickness, counter size | NO SOURCE read (the catalogue lists counters per shop in cm; not extracted) | - | - | NS |

### 1.3 Domus / atrium house (Pompeii, Herculaneum)

What: inward-looking house on a deep plot: fauces - atrium with impluvium - tablinum - garden or peristyle; shops in the front
corners; few and small street windows; mostly one storey with partial upper rooms.

| # | Element | Value | Building | Source | Status |
|---|---|---|---|---|---|
| D1 | plot (whole block) | "approximately 315 by 115 feet" (96 x 35 m), "no shops except the four in front" | House of the Faun, VI.xii | MAU p. ~283 | PR |
| D2 | atrium | "about 30 by 35 Oscan feet" (8.3 x 9.6 m) | House of the Surgeon | MAU p. ~274-275 | PR |
| D3 | atrium | "approximately 53 feet, the breadth 33; the height ... not less than 28 feet" (16.2 x 10.1 x 8.5 m) | House of the Faun | MAU p. ~284 | PR |
| D4 | atrium | "41 by 29 feet" (12.5 x 8.8 m); tablinum 13 ft 9 in wide (4.2 m) | house near the Porta Marina | MAU p. ~285-290 | PR |
| D5 | house extent | "greatest length is not far from 150 feet, the breadth of the excavated portion 130" (46 x 40 m) | House of the Silver Wedding | MAU p. ~294 | PR |
| D6 | room height | rooms about the atrium "often measuring fifteen feet to the edge of the ceiling" (4.6 m); tablinum ceiling "not far from twenty feet" (6.1 m) | pre-Roman houses; House of the Surgeon | MAU pp. ~255, ~275 | PR |
| D7 | door | "the doors measured nearly twelve feet in height" (3.7 m); House of the Faun doors "nearly fourteen feet" (4.3 m) | House of the Surgeon; Faun | MAU pp. ~275, ~255 | PR |
| D8 | storeys | "With few exceptions the houses of pre-Roman Pompeii were built in only one story ... A complete second story was rare"; "Houses with three stories were quite exceptional"; "Sometimes the second story on the front side projected a few feet over the street" (casa del Balcone Pensile) | Pompeii | MAU p. ~267-268 | PR |
| D9 | window | street windows "ordinarily few and small"; "Upper rooms ... windows of good size, sometimes measuring 2 1/2 by 4 feet" (0.76 x 1.22 m; the fraction is damaged in the OCR) | Pompeii | MAU p. ~273 | PR |
| D10 | wall thickness, roof pitch, stair width | NO SOURCE read | - | - | NS |

### 1.4 Strip building (Romano-British / Gallic vicus)

Already grounded in `../m5f_strip_building_grounding.txt` (frontage 6-12 m, length 18-38 m, plots about 10 x 40 m, portico about 2 m /
3-3.35 m, walls 0.5-0.7 m, roof pitch >= 45 degrees, door about 2 x 1 m). Nothing new was found for its NO SOURCE items (eaves and ridge
height, house gap, front-room depth, timber wall thickness, window). One usable cross-reference: T1-T2 give a sourced Italian shop-room
depth of 5.8-6.7 m, the same order as the tree's 6 m DESIGN front-room depth (a proxy from Ostia, not a vicus measurement).

### 1.5 Horreum (warehouse)

What: rows of deep narrow rooms opening on a court or corridor, few external openings, strong doors; in the north-west provinces
a buttressed rectangular granary on a raised floor (no sourced numbers for that form in this pass).

| # | Element | Value | Building | Source | Status |
|---|---|---|---|---|---|
| H1 | overall | "a very large storage building, with an average width of 60 meters" | Horrea di Hortensius, Ostia V,XII,1 | OA regio5/12/12-1.htm | PR |
| H2 | room door | "The average width of the doors of the rooms is 1.70 - 1.80" | Horrea di Hortensius | OA regio5/12/12-1.htm | PR |
| H3 | total area range | "leur superficie totale est comprise entre 838 et 6076 m2" | Ostia warehouses with shops | SCH ch. 6 par. 46 | PR-f |
| H4 | side alley | "a narrow alley (1.20 wide) with a gutter" | Horrea I,VIII,2 | OA regio1/8/8-2.htm | PR |
| H5 | layout | row of shops on the street, ornamental brick portal with pediment, double vestibule, court, two corner staircases (travertine below, ramp above) | Horrea Epagathiana I,VIII,3 | OA regio1/8/8-3.htm (no dimensions on the page) | PR (layout) |
| H6 | room w x d, storey height, wall thickness, provincial granary size | NO SOURCE (Rickman 1971 not opened) | - | - | NS |

### 1.6 Bath (small balneum)

| # | Element | Value | Building | Source | Status |
|---|---|---|---|---|---|
| B1 | whole complex area | "about 1542 m2, reaching deep into the Insula" (a neighbourhood bath, not the smallest class) | Terme del Faro, Ostia IV ii 1 | STO sect. 5.2.1 | PR |
| B2 | large public baths, for scale | 4500 m2 and 7000 m2 | Terme del Foro; Terme di Nettuno | SCH ch. 6 par. 44 | PR-f |
| B3 | basin | "nearly five feet deep" cold basin; "large marble-lined basin ... about 2 feet deep" (WP) | Pompeii baths | MAU p. ~200; WP "Stabian Baths" | PR / PR-2 |
| B4 | road beside a provincial bath | cobbled road "between 2.4 and 3 metres" wide between mansio and bath-house | Letocetum (Wall) | WP "Letocetum" | PR-2 |
| B5 | room sizes, heights, small-balneum footprint | NO SOURCE | - | - | NS |

### 1.7 Temple (small podium temple)

| # | Element | Value | Building | Source | Status |
|---|---|---|---|---|---|
| P1 | small temple | podium "total height 1.78"; pronaos "6.35 x 4.20"; "The cella is square (6.35 x 6.30)" -> about 6.35 x 10.5 m plan (calc) | Tempietto repubblicano, Ostia II,IX,4 | OA regio2/9/9-4.htm | PR |
| P2 | four small temples on one podium | podium "33.50 x 11.55, 1.20 high"; a cella 5.75 x 5.30 | Quattro Tempietti, Ostia II,VIII,2 | OA regio2/8/8-2.htm | PR |
| P3 | medium temple | "The temple measures 31 x 16 metres" | Tempio di Ercole, Ostia I,XV,5 | OA regio1/15/15-5.htm | PR |
| P4 | large (capitolium), upper bound | podium "34.40 x 16.80 m., the cella 19 x 16 m."; preserved height 17 m, original "may have been over 20"; threshold block 6.16 x 1.42 m | Capitolium, Ostia | OA regio1/forum/capitol.htm | PR |
| P5 | temple height | "must have been some 16 m" | Tempio di Roma e Augusto, Ostia | OA regio1/forum/temprom.htm | PR |
| P6 | Pompeii capitolium | podium "10 Roman feet high" (3.0 m), "125 Roman feet long" incl. steps (37 m); columns "about 28 feet high"; doorway "fifteen Roman feet wide" | Temple of Jupiter, Pompeii | MAU p. ~63-65 | PR |
| P7 | provincial comparison | 26.42 x 13.54 m on a 2.85 m podium, door 6.87 x 3.27 m, height 17.1 m | Maison Carree, Nimes | WP "Maison Carree" | PR-2 |
| P8 | provincial comparison | 17.85 x 8.05 m, height 14 m | Temple of Augustus, Pula | WP "Temple of Augustus, Pula" | PR-2 |
| P9 | roof pitch (pediment angle), column spacing | NO SOURCE read | - | - | NS |

### 1.8 Forum and basilica (small-town scale)

| # | Element | Value | Building | Source | Status |
|---|---|---|---|---|---|
| F1 | forum piazza | "measured some 31 by 33 m and was paved with stone slabs" | Caerwent (Venta Silurum), civitas capital | GUE | PR |
| F2 | basilica | "54.5 m from east to west and 20.5 m from north to south"; nave "7.5 m wide", aisles "each 3.75 m wide" | Caerwent | GUE | PR |
| F3 | basilica columns | drum 0.9-0.95 m -> colonnades "something like 9 to 9.5 m high" | Caerwent | GUE | PR |
| F4 | basilica walls | outer walls "0.9 to 0.95 m wide"; partitions "0.8 m wide"; colonnade support walls 1.3 m | Caerwent | GUE p. ~243 | PR |
| F5 | room entrance | open "across almost its full width (5.85 m)" | Caerwent, rear range room | GUE p. 238 | PR |
| F6 | forum ranges | "likely ... two-storeyed, perhaps provided with a balcony above the covered portico" (a reconstruction, not evidence) | Caerwent | GUE | PR |
| F7 | curia | "large room ... (11.50 x 12.00)" | Curia, Ostia I,IX,4 | OA regio1/9/9-4.htm | PR |
| F8 | Pompeii forum | "approximately 497 feet in length by 156 in breadth" incl. colonnade (151 x 48 m); without, 467 x 126 ft (142 x 38 m) | Pompeii | MAU p. ~45-46 | PR |
| F9 | Pompeii basilica | about 180 x 78 English ft (55 x 24 m; "200 Oscan feet" long); main hall "between 35 and 40 feet" high; 28 brick columns | Pompeii | MAU p. ~70-74 | PR |
| F10 | Ostia castrum (the first walled core) | "rectangular military structure (195 x 125.7 metres) with four gates" | Ostia | STO ch. 1 | PR |

### 1.9 Mansio

| # | Element | Value | Building | Source | Status |
|---|---|---|---|---|---|
| M1 | courtyard | "rooms were arranged around a square courtyard approximately 19 by 19 metres"; walls "plaster and daub"; courtyard well 2.29 x 2.44 m, 6.1 m deep | Letocetum (Wall), second mansio | WP "Letocetum" (citing excavation reports not opened) | PR-2 |
| M2 | plan form | courtyard plan, at least 8 rooms in the north range, glazed windows (Wall); courtyard building with own bath block, timber c. AD 120-125 then masonry (Chelmsford) | Wall; Chelmsford | search snippets (roman-britain.co.uk) | SN |
| M3 | overall footprint, range depth, storeys, heights | NO SOURCE | - | - | NS |

### 1.10 Town wall and gate

| # | Element | Value | Building | Source | Status |
|---|---|---|---|---|---|
| W1 | wall, provincial | "about 4.5 metres high" today, "once stood to perhaps 7.8 metres with a wall-walk and parapet"; "At its base the wall was about 3 metres wide" | Silchester | EH | PR |
| W2 | gate, provincial | "The carriageway was about 4 metres wide, sufficient to allow a single cart to pass" | Silchester | EH | PR |
| W3 | wall, Italian | two stone walls "about 15 feet apart and 28 inches thick" (4.6 m; 0.71 m) with earth fill; height "26 or 28 feet" (7.9-8.5 m); inner wall "about 16 feet" higher | Pompeii | MAU p. ~230-231 | PR |
| W4 | towers | "approximately 31 by 25 feet" (9.4 x 7.6 m), "built in two stories" + battlemented terrace | Pompeii | MAU p. ~234 | PR |
| W5 | gate passage | vaulted passage "about twelve feet wide" (3.7 m), open court behind, second vaulted passage with double doors, raised walk on one side | Stabian Gate, Pompeii | MAU p. ~236 | PR |
| W6 | gate towers | square towers, "sides of 4.40 to 4.50 metres"; "sides of 5.85 metres" | Porta Marina; Porta Laurentina, Ostia | OA regio3/pm/pm.htm; regio4/pl/pl.htm | PR |
| W7 | late defences, small town | stone wall "about 2.7 metres" thick, three ditches | Letocetum | WP "Letocetum" | PR-2 |
| W8 | Caerwent wall | up to 5 m high; later towers 8.2 m wide projecting 4.7 m | Caerwent | WP "Venta Silurum" (height); snippet (towers) | PR-2 / SN |

### 1.11 Streets and street furniture

| # | Element | Value | Place | Source | Status |
|---|---|---|---|---|---|
| S1 | street widths, Pompeii | widest "nearly 32 feet" (9.8 m); "Abbondanza and Nola streets, the greatest width of which is about 28 feet" (8.5 m); "the other streets and thoroughfares vary from 10 to 20 feet" (3.0-6.1 m). "Only the principal streets were wide enough to allow wagons to meet and pass" | Pompeii | MAU p. ~221-222 | PR |
| S2 | main street, Pompeii | "Measuring more than 8 meters in width, facade to facade" | Via dell'Abbondanza | J. Hartnett, *The Roman Street* (Cambridge 2017) ch. 8 p. 259 ff. (chapter PDF opened) | PR |
| S3 | main street, Ostia | "rettifilo largo circa 9 metri e lungo ben 820" (Pavolini 2005 p. 197; the street is not named in the quote - context is the Decumanus) | Ostia | SCH ch. 11 fn 43 | PR-f |
| S4 | side alley, Ostia | 1.20 m (H4) | Ostia | OA | PR |
| S5 | pavement (sidewalk) width | "Sidewalk widths vary throughout Pompeii, averaging between 2.5 to 4 meters wide" - high against Hartnett's 1.06-1.71 m (snippet, PRIOR); the two disagree, treat as an unresolved range | Pompeii | ROC sect. 3.B.i (TOC p. 92) | PR (conflicts with a snippet) |
| S6 | pavement, Ostia | "a sidewalk of two rows of bipedales" (a bipedalis is "about 2 Roman feet square") -> about 1.2 m (calc) | Via della Caupona, Ostia | STO p. ~96, fn 25 | PR + calc |
| S7 | pavement make-up | "raised sidewalks, with basalt or tufa curbing"; surface small stones, concrete or beaten earth, varying house by house; streets paved with polygonal basalt | Pompeii | MAU p. ~222 | PR |
| S8 | kerb height | "elevated curbstones averaging .5m in height" (cf. Van Roggen's 34.5 cm mean, snippet, PRIOR) | Pompeii | ROC ch. 2 | PR |
| S9 | stepping stones | "The vast majority of stepping-stones average between 1 - 1.5 meters lengthwise"; some "closer to .5 meters"; one 0.65 m; "typical spacing between two stepping-stones in Pompeii is 45 centimeters"; top level with the sidewalk; "in the broadest streets as many as five" | Pompeii | ROC ch. 2; MAU p. ~222 | PR |
| S10 | public fountain | basin of "four large slabs of basalt, held together at the corners by iron clamps", a pierced standard with a relief spout on one long side, overflow notch opposite; mostly at corners; "for most households a fountain exists within 50m" | Pompeii | MAU p. ~224; ROC ch. 2 | PR (form; no basin dimensions) |
| S11 | water tower | "small pillars of masonry preserved ordinarily to the height of 20 feet" (6.1 m) | Pompeii | MAU p. ~224 | PR |
| S12 | well in a courtyard | "0.90 x 0.70 m" | Caseggiato del Larario, Ostia | OA regio1/9/9-3.htm | PR |
| S13 | provincial roads | Ryknild Street "7.3 metres" wide at Wall | Letocetum | WP | PR-2 |
| S14 | fountain basin size, hitching posts, bollards, benches | NO SOURCE (ROC notes "nine [hitching holes] along a 32.5 meters length") | - | ROC ch. 3 (hitching holes) | NS / PR |

---

## 2. Reference images for image-to-3D

Wikimedia Commons file pages are `https://commons.wikimedia.org/wiki/` + the title below (spaces as underscores). Titles, licences and
authors were read from the Commons API listing on 2026-11-21 through the fetch tool; open each page before use and copy its
attribution line. CC BY / BY-SA need attribution (and share-alike for BY-SA) on any redistributed derivative.
RECON = a modern reconstruction (drawing, model or full-size rebuild): useful for massing, NOT evidence for a dimension.

| Type | Commons file title (or URL) | Shows | Licence, author | Why useful for 3D |
|---|---|---|---|---|
| Insula | File:Insula reconstrustion. Casa di Diana. Gismondi.JPG | 3/4 reconstruction drawing of the Casa di Diana, 4-5 storeys, balconies | CC BY-SA 3.0 claimed by uploader; drawing by Italo Gismondi (d. 1974) in Calza 1923 - the underlying drawing's copyright status is doubtful, check | RECON. Single isolated building, 3/4 view, the standard insula massing |
| Insula | File:Ostia antica - Casa di Diana 01.JPG ; File:Ostia antica - Casa di Diana 02.JPG | standing street front, shop openings with mezzanine windows | CC BY-SA 3.0, Massimop | real elevation of the lower two storeys |
| Insula | File:House of Diana - Ostia Antica.jpg | wide view of the Via di Diana front | CC BY-SA 2.0, Jamie Heath | near-frontal, whole facade length |
| Insula | File:Casa di via della Fortuna. Ostia.JPG | Gismondi reconstruction of another Ostian block | CC BY-SA 3.0 claimed; Gismondi 1923 (same caveat) | RECON. second massing variant |
| Insula | File:OstiaAnticaCaseGiardino.jpg | Garden Houses standing remains | Public domain, MM | row-block type with regular windows |
| Insula | File:Ostia Antica - R3 Caseggiato degli Aurighi P1110136.jpg ; File:Caseggiato degli aurighi 2.jpg | courtyard with tall arcades, upper floors standing | CC BY-SA 4.0 Lalupa; CC0 Rudolfo Kithera | best surviving multi-storey courtyard elevation |
| Insula | https://www.ostia-antica.org/regio1/3/3-3.htm (plan "After Packer 1971, plans 2 and 3"; Gismondi drawing) | measured ground and first-floor plan | site copyright - REFERENCE ONLY, do not redistribute | the only plan of Diana found; scale by its bar |
| Taberna | File:Casa via di Diana.jpg | Gismondi reconstruction, Casa del Termopolio, shops under balcony | CC BY-SA 3.0 claimed; Gismondi 1923 (caveat) | RECON. shop-under-flats facade |
| Taberna | File:House of the Bar 08.jpg ; File:Ostia Antica (46091448084).jpg | Thermopolium on Via di Diana seen from the street | CC BY-SA 2.0 MumblerJamie; CC BY 2.0 Sonse | real shop front, threshold, counter |
| Taberna | File:Thermopolium of Vetutius Placidus opening directly onto the south side of the Via dell'Abbondanza, Pompeii (15042641332).jpg | Pompeian bar counter open to the street | CC BY-SA 2.0, Carole Raddato | single shop, unobstructed |
| Taberna row | File:Pompeii- Road of Abundance, No. 5033 (SM stf1173).png | Via dell'Abbondanza shop fronts, 19th-c. photograph | Public Domain Mark, Giacomo Brogi | long row in perspective, kerbs and paving |
| Domus | File:Plan of House of the Surgeon.png | plan | Public domain, Mau | measured plan of the simplest atrium house |
| Domus | File:Plan of house of Sallustius in Mau fig134.png ; File:Plan of the house of the Faun in Mau fig137.png | plans | Public domain, Mau | orthographic plans, lettered rooms |
| Domus | File:Pompeii, its life and art (1902) (14578124970).jpg | longitudinal SECTION of the House of the Faun | No known restrictions, Internet Archive Book Images | section gives roof and atrium heights (RECON by Mau) |
| Domus | File:Reconstruction drawing House of the Faun, Pompeii.jpg | reconstruction drawing | Public domain, unknown author | RECON. whole-house massing |
| Domus | File:Vitruvius the Ten Books on Architecture - The House of the Surgeon, Pompeii.png | plan / figure from Morgan's Vitruvius (1914) | Public domain | clean line drawing |
| Timber house (upper-floor proxy) | File:Casa a graticcio.jpg ; File:Casa a Graticcio (Ercolano) WLM 001.JPG ; File:House of the Opus Craticium (7254060440).jpg | Herculaneum half-timbered two-storey house with balcony on columns | CC BY-SA 3.0 19francesco84; CC BY-SA 4.0 Mentnafunangann; CC BY-SA 2.0 Hill/Kleerup | the one standing Roman timber-framed two-storey front |
| Strip building | File:Vindobona streifenhaus lagervorstadt.jpg | reconstruction of a strip house (Vienna canabae) | "Copyrighted free use", Veleius | RECON. single isolated strip house, 3/4 view |
| Strip building | File:Grundrisse römischer Streifenhäuser bei Nettersheim-2274.jpg (also -0008, -2294, -2296) | strip-house ground plans marked out on site | CC BY-SA 4.0, Raimond Spekking | real plan proportions side by side |
| Strip building | File:Römische Handwerkerhäuser und Herberge im archäologischen Park Xanten.jpg | full-size rebuilt craftsmen's houses, Xanten | CC BY-SA 4.0, Xjvolker | RECON. gabled street fronts with porch |
| Strip building | File:Schwarzenacker Haus des Augenarztes.jpg | rebuilt vicus house, Schwarzenacker | CC BY-SA 3.0, Lokilech | RECON. single building |
| Horreum | File:Horrea Epagathiana Ostia Antica 2006-09-08.jpg ; File:Ostia, horrea epagathiana 01.JPG | brick portal with pediment and inscription; court | CC BY 2.5 Jastrow; CC BY 3.0 Sailko | frontal portal; real elevation |
| Horreum | File:The reconstructed horreum (grain store - granary) now houses the museum, Saalburg Roman Fort, Limes Germanicus, Germania (Germany) (7957251526).jpg ; File:Horrea.JPG | rebuilt buttressed granary | CC BY-SA 2.0 Carole Raddato; CC BY-SA 3.0 Gorinin | RECON (1900s rebuild). isolated, 3/4 view of the provincial form |
| Bath | File:Pompeii, its life and art (1902) (14578135269).jpg | Mau's page on bath rooms / Stabian Baths | No known restrictions | plan-level reference only; no good isolated small-bath image found |
| Temple | File:EB1911 Temple - Elevation of Maison Carrée at Nîmes.jpg ; File:1911 Britannica-Architecture-Maison Carrée.png | orthographic elevation; elevation + plan | Public domain (R. P. Spiers; EB 1911) | measured orthographic drawings |
| Temple | File:Elévations de la Maison Carrée Nîmes - btv1b10678215n.jpg | large-format elevations (BnF) | Public domain, Grangent / Durand / Durant | high-resolution orthographic |
| Temple | File:Croatia Pula Temple of Roma and Augustus 2014-10-11 11-30-05.jpg ; File:Templo de Augusto, Pula, Croacia, 2017-04-17, DD 71-73 HDR.jpg | small tetrastyle podium temple, front and 3/4 | CC BY-SA 3.0 Berthold Werner; CC BY-SA 4.0 Diego Delso | complete standing small temple, unobstructed |
| Temple | File:APX - Hafentempel 01 ies.jpg ; File:Xanten, Archäologischer Park, Hafentempel, 2018-08 CN-02.jpg | partial full-size rebuild on its podium | CC BY-SA 3.0 Frank Vincentz; CC BY-SA 4.0 Steffen Schmitz | RECON. podium and column proportions, painted |
| Capitolium | File:Capitolium Ostia Antica 2006-09-08 n1.jpg ; File:Ostia Antica Capitolium.jpg | brick podium temple with stair | CC BY 2.5 Jastrow; Public domain Jan Keymeulen | real massing of a tall podium temple |
| Forum / basilica | File:Caerwent Roman Town 20171016 forum basilica.jpg | excavated basilica remains | CC BY-SA 4.0, Otter | plan-level only (low walls) |
| Forum / basilica | File:Pompeii, its life and art (1902) (14764411642).jpg | Mau's Basilica page | No known restrictions | plan reference |
| Forum / basilica | GUE figs 2-5 (plan and reconstruction) doi:10.1017/S0068113X21000489 | measured plan of forum-basilica | CC BY-NC-ND - reference only, no derivatives | the measured small-town plan behind F1-F5 |
| Mansio | File:Xanten, Archäologischer Park, Herberge, 2018-08 CN-01.jpg ; ... CN-02.jpg | full-size rebuilt inn (Herberge) | CC BY-SA 4.0, Steffen Schmitz | RECON. the only isolated 3/4 view of an inn found |
| Town wall / gate | File:Porta Herculaneum Pompeii.jpg | Herculaneum Gate, three passages | CC0, Jebulon | frontal, unobstructed |
| Town wall / gate | File:Pompei. Porta Ercolano Reconstructietekening en overblijfselen, GD004703.jpg | reconstruction drawing beside the remains | Public domain, unknown (KU Leuven slide) | RECON + evidence side by side |
| Town wall / gate | File:Newport Arch.jpg | standing Roman gate arch, Lincoln | CC BY-SA 4.0, Marek69 | provincial single-carriageway arch |
| Town wall / gate | File:CallevaAtrebatum SouthGate.jpg ; File:Perimeter wall of the Calleva Atrebatum (Roman Town).jpg | Silchester gate gap and wall | CC BY-SA 4.0 Martinvl; CC BY-SA 2.0 Peter S | the wall behind W1-W2 |
| Fountain | File:Pompeii, Ancient Roman Fountain at the junction of Via Stabiana and Via dell'Abbondanza (48441020961).jpg ; File:Fountain in Pompeii 01.jpg ; File:Pompeii (9809372414).jpg | basalt slab basin with relief standard | Public domain Gary Todd; CC BY 4.0 Bernard Gagnon; CC0 Gary Todd | single object, 3/4 view |
| Stepping stones / kerb | File:Pompeii, Stepping stones at Via del Tempio d'Iside (48442377296).jpg ; File:Street chaussée Pompeii.jpg ; File:Pompeii Via dell' Abbondanza 01.jpg | crossings, kerbs, ruts | Public domain Gary Todd; CC0 Jebulon; CC BY-SA 4.0 Ad Meskens | prop set: stones, kerb, paving pattern |
| Plans, all Pompeii types | https://archive.org/details/pompeiiitslifear00mauauoft (Mau 1899 figures: forum plan, basilica, temples, baths, gates Fig. 103-105, houses Fig. 109-141) | measured plans, sections, restorations | Public domain (1899) | orthographic plates; restorations are RECON |

Not found with a usable licence: a museum model of an insula (the Museo della Civilta Romana / Gismondi plastico searches returned
nothing usable), a small provincial bath as an isolated building, a measured mansio plan.

---

## 3. Text prompts for a text-to-3D generator

Numbers in each prompt come from section 1 (row ids in brackets). Where a row is NS the prompt says so and the value is a placeholder
to be replaced or labelled DESIGN.

| Type | Prompt |
|---|---|
| Insula, small single-frontage [I1, I9-I19, I24, T7] | "Roman imperial apartment block (insula), Ostia, 2nd century AD. Single isolated rectangular building 16 m wide by 24 m deep, four storeys, about 17.7 m to the eaves. Unplastered orange-red brick-faced concrete walls 0.6 m thick. Ground floor: three wide square-headed shop openings about 3 m wide with travertine thresholds, a small mezzanine window (0.8 m wide, 1 m high) above each, and one narrow street doorway to a staircase. Upper floors: regular rows of plain rectangular windows about 1.5 m wide, a continuous shallow balcony on brick corbels at first-floor level. Low-pitched terracotta tile roof. Plain ground, no people, no vegetation, neutral light." (roof form and upper window size are NOT sourced) |
| Insula, courtyard block [I4, I16, I28] | "Roman courtyard apartment block (caseggiato), Ostia, about AD 150. Single isolated building of about 730 square metres on a street corner, four storeys, brick-faced concrete with ground-floor walls nearly 1 m thick, shops with mezzanine windows along two street fronts, travertine staircase of 16 steps rising from the street corner, small open inner courtyard with a fountain basin, barrel-vaulted corridors, tile roof. Plain ground, no people." (court size NOT sourced) |
| Taberna row [T1-T3, T7, T12-T15] | "Row of eight Roman shops (tabernae) behind a brick-pier portico, Ostia. Each shop one room 4.5 m wide and 6.7 m deep, front fully open with a 3 m travertine threshold grooved for shutters, barrel vault 6.2 m high with a timber mezzanine floor at 3.4 m lit by a small window over the door. Brick-faced concrete, flat roof terrace or low tile roof. Row about 40 m long. Single isolated building, plain ground, no people." (portico depth NOT sourced) |
| Domus [D1-D9] | "Roman atrium house (domus), Pompeii, 2nd century BC to 1st century AD. Single isolated one-storey house on a deep rectangular plot about 12 m wide and 35 m long, blank stuccoed tufa street wall with a tall central doorway 3.7 m high and two shop openings at the corners, very few small high windows. Inward-sloping tiled roofs around a rectangular atrium 8 m by 10 m with a central roof opening and shallow basin, tablinum behind, small walled garden at the rear, a few low upper rooms. Plain ground, no people." (plot width and wall thickness NOT sourced) |
| Strip building [strip-building record] | "Romano-British roadside strip building, 2nd century AD. Single isolated long narrow house 8 m wide and 24 m long, gable end to the road, timber-framed walls on a low rubble plinth with daub infill, steep 45-degree roof of red tile, open timber porch 2 m deep across the front, wide shop opening and door in the gable, yard with a well behind. Plain ground, no people." (eaves height and windows NOT sourced) |
| Horreum [H1-H5] | "Roman warehouse (horreum), Ostia, about AD 150. Single isolated two-storey brick building around a small arcaded court, almost windowless outer walls, monumental brick doorway with engaged columns and a triangular pediment, rows of deep narrow storerooms with doors 1.75 m wide, corner staircases. Plain ground, no people." (overall size and heights NOT sourced) |
| Small bath [B1] | "Small Roman neighbourhood bath-house (balneum), 2nd century AD. Single isolated low building of brick-faced concrete, a row of three barrel-vaulted rooms of increasing warmth with small high windows, an apse on the hot room, a stoke-hole lean-to and chimney vents, a tiled entrance porch. Plain ground, no people." (ALL dimensions NOT sourced) |
| Small podium temple [P1, P8] | "Small Roman podium temple, tetrastyle prostyle. Single isolated building about 6.4 m wide and 10.5 m long on a moulded tufa podium 1.8 m high, frontal flight of steps, porch of four columns 4.2 m deep, square windowless cella 6.3 m square with one tall central door, low-pitched gabled tile roof with a plain pediment. Plain ground, no people." (column height and pediment pitch NOT sourced) |
| Forum-basilica, small town [F1-F6] | "Romano-British small-town forum and basilica, early 2nd century AD. Single isolated complex: a paved square 31 m by 33 m enclosed on three sides by a single-storey colonnaded portico with shops behind, the fourth side closed by a basilica hall 54.5 m long and 20.5 m wide with a tall nave 7.5 m wide on two rows of columns 9 m high, lower side aisles, a range of rooms behind, clerestory windows, red tile roofs, mortared stone walls 0.9 m thick. Plain ground, no people." (overall height and portico depth NOT sourced) |
| Mansio [M1] | "Roman roadside official inn (mansio), Britain, 2nd century AD. Single isolated one- and two-storey building of four ranges around a square courtyard 19 m by 19 m with a central well, timber-framed and plastered walls on stone footings, tiled roofs, internal veranda around the courtyard, one wide carriage entrance from the road, a small attached bath block at one corner. Plain ground, no people." (range depth, heights and storeys NOT sourced) |
| Town wall with gate [W1-W6] | "Section of a Roman town wall with a single gate, 3rd century AD. Straight masonry curtain wall 3 m thick at the base and 7.8 m high to a crenellated parapet with a wall-walk, a single arched carriageway 4 m wide flanked by two square towers about 4.5 m on each side rising one storey above the wall. Flint and stone rubble with tile bonding courses. Isolated segment 40 m long, plain ground, no people." |
| Street fountain (prop) [S10] | "Roman public street fountain, Pompeii. Rectangular open basin made of four upright basalt slabs clamped at the corners with iron, a short thick stone standard on one long side carved with a relief head whose mouth is the water spout, worn rim, standing on a street corner kerb. Single isolated object, plain ground." (basin dimensions NOT sourced) |
| Stepping stones and kerb (prop) [S7-S9] | "Roman street segment 12 m long: polygonal dark basalt paving with wheel ruts, raised sidewalks on both sides behind basalt kerbstones 0.5 m high, three large oblong stepping stones with rounded corners 1.2 m long set across the carriageway 0.45 m apart, tops level with the sidewalks. No buildings, no people." |

---

## 4. What remains unsourced

1. An excavated insula's footprint as w x d for the canonical examples (Casa di Diana, Caseggiato del Serapide / degli Aurighi, the
   Garden Houses blocks, the Casette-tipo). Only the Caupona del Pavone (16 x 24 m) and building AREAS were page-read. Packer 1971 is
   the source and was not reachable; the OA plans have scale bars but reading a plan off a picture is a measurement of mine, not a source.
2. Storey height above the first floor (only a 4.43 m mean derived from 17.70 m / 4).
3. Stair WIDTH in an insula (tread 0.29 and riser 0.20 are sourced; a shop ladder is 0.77 m wide).
4. Medianum and courtyard dimensions; balcony projection; portico depth at Ostia.
5. Insula roof form and pitch; upper-floor window size (Packer's 1.5-2.0 x 0.9-1.3 m is still a snippet).
6. Pavement width: two page-read values disagree (2.5-4 m Rocchio; about 1.2 m Via della Caupona) and Hartnett's 1.06-1.71 m is still a snippet.
7. Storeys and footprint of a provincial (non-Ostian) insula - no evidence found that the type existed in a Romano-British or Gallic small town.
8. Small bath, mansio and provincial granary: plan dimensions, heights.
9. Fountain basin dimensions; any street furniture beyond fountain, stepping stones, kerb, water tower.
10. Everything already NO SOURCE for the strip building (eaves, ridge, house gap, timber wall, window).

## 5. What this unblocks

| Item | Now supportable from page-read sources | Still a DESIGN pick |
|---|---|---|
| Block / street grid for a town tier | main street 8.5-9.8 m (S1-S3), ordinary streets 3.0-6.1 m (S1), alley 1.2 m (S4), kerb 0.5 m (S8), one whole Ostian block 7321 m2 holding 14 buildings (I3), forum piazza 31 x 33 m with a 54.5 x 20.5 m basilica (F1-F2); block sizes from the first pass | which block size; pavement width (conflict, item 6) |
| Party-wall mechanism | both cases are attested in one block: buildings standing "wall on wall" / sharing "a common wall" (STO, IV ii 7-8, 12) AND a building that "does not share party walls with its northern and southern neighbours" (I1); wall 0.6 m for a multi-storey block, 0.9-1.0 m for the tallest (I16-I19); 0.44 m legal cap (PRIOR) | when a generated lot shares and when it abuts |
| Multi-storey insula typology | 16 x 24 m footprint (one building), 4 storeys / 17.7 m (one complex), ground storey 3.4-3.9 m with vault to 6.2-6.5 m, walls 0.6 m, street stair with 0.29 / 0.20 steps and a 16-tread first flight, flats 150-220 m2 with a medianum | upper storey height, stair width, court size, roof, upper windows |
| Taberna-below-flats typology | shop 3.8-5.4 m wide x 5.8-6.7 m deep, mean 35.6 m2, opening mean 2.93 m (2-4 m), threshold 0.25 m high, mezzanine at 3.4 m with a 0.8 x 1.0 m window, back-room door 0.9-1.4 m, ladder 0.77 m | shop wall thickness, counter |
| Window rule | Roman values now exist where the tree uses the medieval 1 x 1 rule: mezzanine 0.6-0.8 x <= 1.0 m (I24), medianum facade window 1.5 m wide (I23), ground-floor window 1.05 m high starting 0.20 m above the floor (I25), Pompeian upper room 0.76 x 1.22 m (D9), street fronts of houses "few and small" (D9) | sill heights and spacing; the upper-floor insula window |
| Era street furniture / props | fountain form, stepping stones 1-1.5 m long at 0.45 m gaps and up to five across, kerb 0.5 m, polygonal basalt paving with ruts, 6.1 m water tower, courtyard well 0.9 x 0.7 m | fountain basin size |
