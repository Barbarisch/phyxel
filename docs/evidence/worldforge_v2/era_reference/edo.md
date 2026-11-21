# Era reference dossier — EDO-period Japan (1603–1868): castle town, post town, farm village

First pass, 2026-11-21. Breadth over depth. Purpose: starting points for a WorldForge era pack
(`docs/WorldForgeV2Plan.md` D9, D39, section 14). Nothing here is typed into engine data yet; every number
must go through the grounding-auditor again before use.

## 0. How to read this file

| Code | Meaning |
|---|---|
| **PR** | I fetched the page/PDF text and read the sentence myself. Primary or specialist source. |
| **PR-T** | Page read by me, but TERTIARY (Wikipedia en/ja, popular site). A lead; replace before shipping. |
| **SN** | Search-result snippet only; page not opened. Not usable as a source. |
| **NS** | NO SOURCE found in this pass. Do not use a number. |

"conv." = my unit conversion; "derived" = computed from sourced numbers. I did **not view any image**.
Plan terms: **桁行 (ketayuki)** = length along the ridge; **梁間 (harima)** = span across the ridge.
"Bunka DB" = Agency for Cultural Affairs, Cultural Heritage Online / 国指定文化財等データベース record,
`https://online.bunka.go.jp/heritages/detail/<id>` — an official record with measured plan dimensions.

## 1. Units and module (the era's grid)

| Unit | Value | Source | Status |
|---|---|---|---|
| shaku 尺 | 30.3 cm | JAANUS, "ken", https://www.aisf.or.jp/~jaanus/deta/k/ken.htm | PR |
| ken 間 (bay) | 6.5 shaku = 197 cm in Hideyoshi's time; "reduced to 6.0 shaku, or 181.8 cm" around 1650; bays "usually just under 2m" | JAANUS "ken" | PR |
| kyouma 京間 (Kansai) | bay 6.5 shaku (196.95 cm) measured BETWEEN pillars; Kyoto tatami 6.3 x 3.15 shaku (190.89 x 95.445 cm) | JAANUS, "kyouma", .../deta/k/kyouma.htm | PR |
| inakama / edoma 田舎間 (Kanto, Tohoku) | bay 1.8 m or less, pillar CENTRE to centre; tatami up to 176 x 88 cm | JAANUS, "inakama", .../deta/i/inakama.htm | PR |
| tatami (general) | about 1.82 x 0.92 m, 5.25 cm thick | JAANUS, "tatami" | PR |
| tatami by region | kyoma 1.91 x 0.955 m; chukyoma 1.82 x 0.91 m; edoma 1.76 x 0.88 m; thickness 5.5 cm (kyoma) / 6.0 cm (edoma) | Wikipedia "Tatami" | PR-T |
| tsubo 坪 | 1 ken x 1 ken = 3.3058 m2 | JAANUS, "tsubo" | PR |
| jou 丈 | 10 shaku = 3.03 m | JAANUS, "jou" | PR |
| chou 町 (length) | 60 ken = 109 m | JAANUS, "chou", .../deta/c/chou.htm | PR |
| Room sizes by mats | 4.5 mats = 9 x 9 shaku (2.73 x 2.73 m); 6 mats = 12 x 9 shaku (3.64 x 2.73 m); 8 mats = 12 x 12 shaku (3.64 x 3.64 m) | Wikipedia "Tatami" (Nagoya-size examples) | PR-T |
| Six-mat room (observed 1880s) | "about nine feet by twelve"; mats "three feet wide and six feet long" | E. S. Morse, *Japanese Homes and Their Surroundings* (1886), archive.org `japanesehomesthe00morsuoft` (OCR text; page not resolved) | PR |

**Trap for the pack:** kyoma measures the clear space between posts, edoma measures post centre to centre, so
the same "6-mat room" is a different size in Kyoto and Edo. Pick ONE system per region.

## 2. Settlement types and layout

| Type | Layout (sourced) | Size data | Source | Status |
|---|---|---|---|---|
| Edo commoner quarter (machi) | Square block between streets, divided in three; front lots (omote-dana) on the street, rear tenements (ura-nagaya) inside | block 60 ken (kyoma) street to street; front plot depth 20 ken; front shop depth about 5 ken; front plot 3 ken wide gets rear nagaya on one side, 5 ken on both | ja.wikipedia 「町屋 (商家)」 | PR-T |
| Edo main streets after the 1657 fire | Planned widths in commoner land | Nihonbashi-dori 18.18 m; Honcho-dori 13.79 m | Tokyo Metropolitan Government, Bureau of Urban Development, "Urban Development in the Edo Period", Introduction 03 (PDF `en_0_03.pdf`, toshiseibi.metro.tokyo.lg.jp) | PR |
| Edo merchant frontages | Large stores merged several plots | frontages of 15 ken and 36 ken recorded; under-eave walkway (mise-shita) 1 ken wide; upper-floor wall set back 3 shaku from the ground floor in Edo (flush in Kyoto/Osaka) | ja.wikipedia 「町屋 (商家)」 | PR-T |
| Kyoto townhouse plots | Narrow deep "eel bed" plots | standard frontage 3–3.5 ken ("approximately 6m"); large houses over 10 ken (18–20 m); classes of 10+ ken, 4–5 ken and 1.5–2 ken | JAANUS, "machiya", .../deta/m/machiya.htm (first two); ja.wikipedia (classes) | PR / PR-T |
| Kyoto plots (other statement) | — | 5.4–6 m wide, 20 m deep | Wikipedia "Machiya" | PR-T |
| Post town (shukuba) on a highway | One street lined both sides; honjin, waki-honjin, hatago, toiya | Tokaido width ordered at 5 ken in Keicho 9 (1604); ichirizuka mounds 5 ken square each ri | ja.wikipedia 「東海道」, 「一里塚」 (the ichirizuka page gives its height as "1 jo (about 1.7 m)", which contradicts 1 jo = 3.03 m — unresolved) | PR-T |
| Highways, general | — | Tokaido "averaged 5.5 metres" (Britannica); Five Highways "five to nine meters" (nippon.com) | search summaries | SN |
| Post town extent | Narai-juku preservation district | about 1 km N–S by 200 m E–W | Wikipedia "Narai-juku" | PR-T |
| Post-town frontages (measured buildings) | Sanayama house, Mochizuki-juku (toiya + hatago, 1766): 10.9 m ridge length x 17.8 m span; Kinomoto honjin: frontage 6.5 ken; Morikawa house: frontage 7 ken | see section 3 | Bunka DB 121784, 550905, 120084 | PR |
| Castle town (jokamachi) | Castle, samurai quarters, merchant/artisan quarters below, temples at the edge; gate squares (masugata) | typical masugata "approximately 9m by 14.5m (5 ken by 8 ken)" | JAANUS, "masugata", .../deta/m/masugata.htm | PR |
| Farm village (Shirakawa-go, Ogimachi) | Houses scattered on a river terrace, ridge lines parallel | terrace about 1500 x 350 m; lanes 2–4 m wide (Edo-period network) | Wikipedia "Historic Villages of Shirakawa-gō and Gokayama" | PR-T |
| District gates (kido) | Timber gate across the street between machi, wicket in one leaf, shut at night | dims NS | JAANUS, "kido" | PR (form) / NS |
| Back alley (roji) width; well, privy, shrine in the yard | Shared well, shared privy, refuse bin, Inari shrine | alley width **NS** | search summaries | SN / NS |

## 3. Building types

| # | Type | Plan form | Footprint (measured examples) | Bay / module | Storeys | Walls | Eaves / ridge | Roof | Door | Window | Source | Status |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | Machiya (townhouse) | Earth-floored through-passage (tooriniwa) down one side in Kansai; front earth floor (maedoma) in Kanto; raised rooms in line; shop at front; kura at rear | Takagi house, Imai-cho: 11.4 x 12.8 m, 2 storeys, stepped gable, tile. Kawai house, Imai-cho: 13.1 x 13.5 m, 2 storeys. Morimoto house, Otsu: 9.0 x 16 m, 2 storeys, 1 row of 4 rooms beside the passage. Morikawa house (1855): frontage 7 ken, low "tsushi" upper floor | ken (section 1) | 1 + low attic ("tsushi-nikai") to full 2 | timber frame; later thick plaster (ookabe) for fire | NS | eaves to the street (hirairi) usual; early shingles held by stones or thatch, later tile | great door (oodo) into the doma | front lattice (koushi); plastered slit windows (mushiko-mado) above | JAANUS "machiya" (form); Bunka DB 146308, 157155, 161324, 120084 (dims) | PR |
| 2 | Ura-nagaya (back-alley row house) | Units in a row under one ridge, each: small earth floor with stove and sink, one matted room | unit 9 shaku x 2 ken (about 2.7 x 3.6 m), about 6 mats in all: 1.5 mats earth floor + 4.5-mat room; posts 2 sun square | 9 shaku / 2 ken | 1 | thin timber, built to be pulled down in a fire | NS | NS | "3-shaku (about 90 cm) wide" sliding door (popular article) | NS | ja.wikipedia 「長屋」, 「町屋 (商家)」; japanese-wiki-corpus "Nagaya (long house)"; door width from a Newsweek Japan snippet | PR-T / SN (door) |
| 3 | Minka (farmhouse), thatched | Large earth-floored doma (cooking, stable) + raised board-floored hiroma with open hearth + matted rooms | Former Nakamura house, Nagano (1698): 25.5 x 10.9 m, hipped, thatch. Nakamura house, Shiga (mid-Edo): 22.8 x 7.7 m, hip-and-gable, thatch. Takizawa honjin Yokoyama house (1661–1750): 20.1 x 7.6 m, thatch | ken | 1 | timber frame, mud-plaster infill | NS | hipped or hip-and-gable thatch; thatch about 35 cm thick, 90 cm or more at the eaves | NS | NS | Bunka DB 193680, 400458, 192860 (dims); JAANUS "kayabuki" (thatch) | PR |
| 3a | Minka details | Raised floor; hearth; veranda; stable floor | raised floor about 50 cm above the doma (Wikipedia) / "a foot and a half or two feet above the ground" (Morse); irori average 90 x 90 cm, up to 2 x 1 m; engawa commonly 1–1.3 m wide; stable floor dug about 25 cm below ground; shoji posts about 1.82 m apart | — | — | — | lintel (kamoi) to ceiling "eighteen inches or more" (Morse) | thatch eaves "two feet or more in thickness" (Morse) | — | — | Wikipedia "Minka", "Engawa", "Shoji" (PR-T); JAANUS "irori", "doma" (PR); Morse 1886 (PR) | mixed |
| 4 | Gassho-zukuri farmhouse | Very steep thatched gable roof with 2–3 loft floors for silkworms | Former Odo house, Shirakawa (1833): 21.1 x 12.3 m, "one storey, three levels" (一重三階), gable, thatch | ken | 1 + 2 lofts (3–4 levels) | timber frame, board/plaster | NS | steep gable thatch; pitch **NS** | NS | gable-end paper windows; NS | Bunka DB 200029; Wikipedia "Historic Villages of Shirakawa-gō and Gokayama" (levels) | PR / PR-T |
| 5 | Kura / dozo (fireproof storehouse) | Small thick-walled box, 2 storeys, few openings | Ashida dozo: 5.9 x 3.9 m, 2 storeys ("a standard dozo"). Hakogi dozo (1818–30): 4.925 x 3.94 m, 38.8 m2 total floor. Nishikawa dozo (1681–83): 6.1 x 3.9 m, 3 storeys. Kawai inui-gura: 11 x 3.8 m. Komori dozo: 12 x 4.2 m | NS | 2 (3 as a status symbol) | timber frame with mud daub "about 20-30cm thick", white or black plaster; lower wall often boarded; granite footing | NS | gable, tile (hongawara or sangawara); separate outer roof in some districts | thick plastered double swing doors (kannonbiraki) | few, small, plastered shutters | JAANUS "dozou"; Bunka DB 173212, 447230, 146611, 187693, 206534 | PR |
| 6 | Hatago / honjin (inns) | Hatago: shop-like front, guest rooms upstairs, owner + kitchen below. Honjin: house of a leading townsman + shoin-style reception suite with formal porch (genkan/shikidai), raised room (jodan-no-ma), front gate | Ota waki-honjin (1769/1773): living part 14.3 x 17.9 m, 2 storeys, gable with udatsu; reception part 5.6 x 9.4 m. Sabanami honjin (1804–17): living part 18.1 x 13.7 m, reception part 10.0 x 7.4 m. Sanayama toiya-hatago (1766): 10.9 x 17.8 m + wing 9.8 x 4.6 m. Honjin building area "often about 200 tsubo" (c. 660 m2, conv.) | ken | 1–2 | timber, plaster | NS | gable, tile or shingle | honjin: gate + shikidai porch; hatago: open shop front | lattice | JAANUS "honjin", "hatagoya" (form); Bunka DB 199989, 184916, 121784; ja.wikipedia 「本陣」 (200 tsubo, PR-T) | PR |
| 7 | Hinomi yagura (fire watchtower) | Open timber frame on four earth-fast posts canted inward, platform with railing and small roof | about 9 m high; platform about 1 tsubo (2 x 2 m); posts set about 1 m into the ground; "one about every ten blocks" in Edo | — | — | open frame, diagonal bracing | 9 m | shingle or bark | ladder | — | JAANUS, "hinomiyagura", .../deta/h/hinomiyagura.htm | PR |
| 8 | Gates | (a) kido street gate: two earth-fast posts, pair of swing doors, wicket. (b) yakuimon: samurai-house gate, 1 bay (3 bays for mansions). (c) nagayamon: gate through a row-house range. (d) masugata square with koraimon + yaguramon at castles | masugata 9 x 14.5 m (5 x 8 ken); temple romon 3 x 2 ken common, up to 5 x 2 ken; gate leaf and post sizes NS | ken | 1–2 | timber | NS | gable, tile | — | — | JAANUS "kido", "yakuimon", "masugata" (PR); Wikipedia "Mon (architecture)" (PR-T) | PR / PR-T / NS |
| 9 | Torii | Two posts, two lintels (myojin type has upswept top lintel) | one dated example: Nakayama Shrine torii, 9 m tall (1791). Typical village torii size and proportions: **NS** | — | — | timber or stone | 9 m (one large example) | — | — | — | Wikipedia "Torii" | PR-T / NS |
| 10 | Temple hall (hondo) / shrine hall | Square-ish hall measured in bays, core (moya) + 1-bay aisle (hisashi) | example of the bay system: Zen butsuden 5 x 5 ken outside over a 3 x 3 ken core. Edo-period village temple dimensions: **NS** | ken | 1 | timber, plaster | NS | hip-and-gable or pyramidal, tile | — | — | Wikipedia "Ken (unit)" | PR-T / NS |
| 11 | Well, bathhouse, notice board (kosatsu-ba), ichirizuka | Well: timber- or stone-lined shaft, pebble bottom; communal tubs in Shiga "about 1.5m high and 4.5m in circumference"; wells "thirty or forty feet" deep (Morse). Ichirizuka 5 ken square | well-curb size, kosatsu-ba size, sento size: NS | — | — | — | — | — | — | — | JAANUS "ido" (PR); Morse 1886 (PR); ja.wikipedia 「一里塚」 (PR-T) | mixed |

**Not found anywhere (all NS):** eaves and ridge heights for every type; roof pitches (tile, thatch, gassho);
storey heights; door and shoji leaf sizes other than the 3-shaku nagaya door (snippet) and the 1.82 m post
spacing; lattice bar sizes; alley widths; kido, torii, kosatsu-ba dimensions; fence and wall heights.

## 4. Reference images for image-to-3D

Commons page URL = `https://commons.wikimedia.org/wiki/` + file title (spaces as underscores). Author/licence
as returned by the Commons API on 2026-11-21. Photographs are of surviving or relocated original buildings
unless marked.

| Type | File (Commons) | Author | Licence | Shows (from title/description) | Why useful |
|---|---|---|---|---|---|
| Machiya | File:Machiya.png | Yoichi Nakanshi | CC BY 2.5 | Hata house, Aburanokoji, Kyoto | single facade |
| Machiya | File:Kyoto Machiya - panoramio.jpg | ccfarmer | CC BY 3.0 | Kyoto machiya | facade |
| Machiya | File:Facade of a dwelling with sudare, wooden balustrades and yellow lamp, Shinbashi-dori, Gion, Kyoto, Japan.jpg | Basile Morin | CC BY-SA 4.0 | Gion two-storey facade, lattices and blinds | high-detail elevation |
| Machiya (period drawing) | File:Machiya of Kyoto and Osaka.jpeg; File:Large-scale Machiya of Kyoto and Osaka.jpeg | Kitagawa Morisada (*Morisada Manko*, 19th c.) | Public domain | Edo-period drawings of ordinary and large Kyoto/Osaka machiya | PERIOD source, elevation-like, PD |
| Machiya (period drawing) | File:House in Tokio Morse 1885 Figure 49.png | E. S. Morse | Public domain | Tokyo house, 1885 engraving | PD perspective drawing |
| Nagaya | File:Edo Fukagawa 3.JPG | DryPot | CC BY 2.5 | Fukagawa Edo Museum tenement row (**modern museum reconstruction**) | the only full ura-nagaya row found |
| Nagaya | File:Fukagawa Edo Museum on the 30th of october 2010 - 77.jpg (also 78, 80, 81, 83, 84, 89) | Lionel Allorge | CC BY-SA 3.0 | Fukagawa Edo Museum street set (**modern reconstruction**, indoor) | many angles of one set; alley, well, shop fronts |
| Minka | File:Nihon Minka-en 4.jpg (also 11, 30, 53, 55, 57, 58, 70, 79, 80) | Syced | CC0 | relocated farmhouses at Nihon Minka-en, Kawasaki | CC0 set of original buildings; which house is which must be checked by eye |
| Gassho | File:Shirakawa Gassho-zukuri 08.jpg (also 14, 19, 22, 23, 24, 26, 39) | Zairon | CC BY-SA 4.0 | Shirakawa gassho houses | many single-house views |
| Kura | File:Yagi Dozo,Kawachinagano.jpg | Ty19080914 | CC BY-SA 4.0 | registered dozo | isolated storehouse |
| Kura / fireproof shop | File:Kawagoe Kuratsukuri Museum.JPG | Abasaa | Public domain | Kawagoe kura-zukuri shop (note: Kawagoe's kura street is largely post-1893 fire, i.e. Meiji — from memory, verify) | PD; heavy plastered shop-storehouse |
| Kura street | File:Kurazukuri Street in Little Edo.jpg | Dandy1022 | CC BY-SA 4.0 | Kawagoe street | street-scale massing |
| Hatago | File:Echigawa-juku hatago.jpg | Douggers | CC BY 3.0 | hatago on the Nakasendo | inn facade |
| Hatago | File:Matsuzaki-shuku hatago 01.jpg (also 02) | STA3816 | CC BY-SA 4.0 | hatago, Ogori | two views |
| Hatago | File:Hatago Hakuchu ac (1).jpg (also (3)) | Asturio Cantabrio | CC BY-SA 4.0 | machiya-form inn with udatsu and mushiko windows, Nagahama | details named in the description |
| Hatago (period print) | File:Seki Gyosho Tokaido.jpg | Utagawa Hiroshige | Public domain | "Seki: The Inn", woodblock print | PERIOD view of an inn front, PD |
| Hinomi yagura | File:Ōuchijuku Hinomi-yagura 大内宿 火の見櫓 - panoramio.jpg | abexxxx | CC BY-SA 3.0 | watchtower at Ouchi-juku | timber tower in a post town |
| Hinomi yagura | File:Tokorozawa-shinden Inari-jinja yagura.jpg | tokorokoko | Public domain | fire lookout with alarm bell | PD |
| Gate (nagayamon) | File:Odake nagayamon01 2816.jpg | 663highland | CC BY-SA 3.0 | Kaibara domain elder's nagayamon, built 1714 | dated Edo gate |
| Gate (nagayamon) | File:Kato House Kameyama ac (1).jpg (also 3–6) | Asturio Cantabrio | CC BY-SA 4.0 | Kato house nagayamon, Kameyama | five views |
| Torii | File:Kumano-hongû-taisha Shrine - Wooden torii.jpg | Yanajin33 | CC BY-SA 3.0 | wooden torii | timber form |
| Torii | File:Himure Hachiman-gū Shrine 20220227 01.jpg | 先徒隗始 | CC0 | myojin torii with shrine name post | CC0 |
| Construction drawings | File:JapanHomes008 Framing.jpg; File:JapanHomes103 SECTION THROUGH VERANDAH AND GUEST-ROOM (SVG).svg; File:JapanHomes152 SHōJI-FRAME FOR WINDOW.jpg | E. S. Morse (SVG redraw CC BY-SA 4.0; others Public domain) | PD / CC BY-SA 4.0 | roof framing; section through veranda and room; shoji frame | the only SECTION drawing found — good for floor/veranda/eaves relationships |

Not found with a usable licence in this pass: measured plans/elevations (the Bunka DB records hold drawings in
printed repair reports, 修理工事報告書, not online); honjin exterior; kido gate; kosatsu-ba; well.

## 5. Text-to-3D prompts

| Type | Prompt |
|---|---|
| Machiya | Edo-period Japanese merchant townhouse (machiya), Kyoto, 19th century. Narrow two-storey timber house with a low upper floor, dark wooden lattice across the ground-floor front, plastered upper wall with slit windows, grey tiled roof with eaves facing the street, small tiled pent roof over the entrance. Single isolated building, plain ground, no people. |
| Ura-nagaya | Edo-period back-alley tenement row (nagaya), Edo, 19th century. Long single-storey timber row house under one low board-and-shingle roof, a line of identical narrow units each with a paper sliding door and a small earth-floor entry, weathered unpainted boards. Single isolated building, plain ground, no people. |
| Minka | Edo-period Japanese farmhouse (minka), 18th century. Large single-storey timber house with a very thick hipped thatched roof and deep eaves, earth-plastered walls, wide sliding wooden doors at one end, open veranda along the front on a raised floor. Single isolated building, plain ground, no people. |
| Gassho farmhouse | Gassho-zukuri farmhouse, Shirakawa-go, Japan, early 19th century. Huge steep thatched gable roof like praying hands reaching almost to the ground, three rows of small paper windows in the timber gable end, low plank walls. Single isolated building, plain ground, no people. |
| Kura | Edo-period Japanese fireproof storehouse (dozo/kura). Small tall two-storey box with thick white plaster walls, dark vertical board cladding on the lower wall, stone footing, tiled gable roof, one heavy plastered double door and one small shuttered window. Single isolated building, plain ground, no people. |
| Hatago inn | Edo-period post-town inn (hatago) on the Nakasendo, 19th century. Two-storey timber building with an open shop-like ground floor, upper floor with a projecting balcony and lattice, low-pitched tiled or shingle roof with stones, eaves facing the road. Single isolated building, plain ground, no people. |
| Honjin | Edo-period honjin (official inn for daimyo), post town, Japan. Large low timber mansion with a tiled gate, a formal gabled entrance porch, long tiled roofs, plastered walls, attached reception wing. Single isolated building, plain ground, no people. |
| Hinomi yagura | Edo-period wooden fire watchtower (hinomi yagura). Tall open timber frame on four posts leaning slightly inward, diagonal braces, ladder, small railed platform at the top under a little shingle roof with a hanging bell. Single isolated structure, plain ground, no people. |
| Nagayamon | Edo-period samurai residence gate (nagayamon). Long single-storey plastered row building with a central double-leaf timber gate and a small side door, dark boards on the lower wall, tiled hipped roof. Single isolated building, plain ground, no people. |
| Torii | Wooden Shinto shrine gate (myojin torii), Japan, Edo period. Two round timber posts, a straight tie beam and an upward-curving top lintel, unpainted weathered wood on stone bases. Single isolated structure, plain ground, no people. |
| Village temple hall | Small Edo-period Buddhist temple main hall (hondo), rural Japan. Square timber hall on a low stone platform, veranda all round, large tiled hip-and-gable roof with wide upturned eaves, front steps under a projecting porch roof. Single isolated building, plain ground, no people. |

## 6. Materials, props and street furniture (draft list; descriptive, not sized)

**Materials named in the sources read:** timber post-and-beam frame; mud daub (tsuchikabe); white and black lime
plaster (shikkui); namako-kabe tile-and-plaster wall facing; fired roof tile (hongawara, sangawara); shingles
held with stones; thatch (kaya) on bamboo purlins; tatami; paper (shoji); board shutters (amado); granite footings
and stepping stones; bamboo.

**Prop candidates (from general knowledge, NOT sourced in this pass; each needs a museum or period-print
reference before modelling):** noren shop curtain; sliding lattice panels; shop signboard (kanban); andon lantern; stone
lantern; rain barrel and fire buckets; fire ladder and alarm bell (hansho); communal well with roof and bucket
(tsurube); refuse bin; small Inari shrine with red torii; notice-board stand (kosatsu); milestone mound with tree
(ichirizuka); stone road marker; roadside jizo statues; palanquin (kago); handcart (daihachi-guruma); rice bales
(tawara); sake barrels (komodaru); irori hearth with hanging pot hook (jizai-kagi); kamado stove; low table,
floor cushions, futon, tansu chest, hibachi; straw sandals and geta at the entrance step; drying racks; bamboo
fence; plank bridge; rice-drying frames; water wheel; stone-edged drainage channel with plank cover (dobu-ita).

## 7. What is unsourced, and cost of a full pack

**Unsourced after this pass:**

1. All heights (eaves, ridge, storey, floor-to-lintel) and all roof pitches.
2. Alley (roji) width, street widths in ordinary post towns (only a 1604 order and two Edo main streets found).
3. Opening sizes (door, shoji, amado, lattice bars, kura door).
4. Dimensions of torii, kido, kosatsu-ba, well-curb, bathhouse, village temple and shrine halls.
5. The Edo 60-ken block and 20-ken plot depth rest on ja.wikipedia only; the scholarly source (Tamai Tetsuo,
   *Edo: ushinawareta toshi kukan*, 1986; AIJ journal vol. 59 no. 464, 1994 — titles from search summaries, not opened) must be read.
6. The ichirizuka height conflict (1 jo vs "about 1.7 m").

**Best route to close the gaps:** the Bunka DB gives plan dimensions for hundreds of dated Edo buildings by type
(search by 町家, 土蔵, 本陣, 長屋門, 鳥居, 本堂); heights and sections are in the printed repair reports
(修理工事報告書) and in Nihon Minka-en / Edo-Tokyo Open Air Architectural Museum house reports.

**Work relative to the medieval pack (my judgement from the plan text; I did not read the generator code):**

| Item | Analogue in the existing generator? | Note |
|---|---|---|
| Narrow deep street plots, front shop + rear yard | Yes — burgage plots are the same idea | the plan itself named the machiya as the alternative second era (D39) |
| A strict modular grid (ken / tatami) | Partly | the pack needs rooms as whole-mat multiples; a constraint the medieval pack does not have |
| Raised timber floor about 0.5 m over an earth-floor zone inside one building | No | two floor levels on one storey; step-up at the doma edge |
| Sliding screens (shoji, fusuma, amado) instead of hinged doors and fixed walls | No | new opening class; walls that are mostly removable panels |
| Engawa veranda under deep eaves | No | eaves overhang far beyond the wall line |
| Low attic upper floor (tsushi-nikai) | Partly | half-height storey |
| Thick curved thatch, hip-and-gable (irimoya) and tile roofs with upturned eaves | No | new roof forms; tile course material needed |
| Plaster-box storehouse | Yes, nearly | simple thick-walled box |
| Rear tenement rows inside a block, reached by an alley | No | a block-interior sub-layout (plots within plots) |
| Post town on one street; castle town with zoned quarters and dog-leg gates | Partly | linear town is easy; zoned castle town is new |
| Free-standing structures: torii, watchtower, notice board | No, but trivial | props or tiny programs |

Overall: the **largest** build of the three — the plot logic transfers, but floors, openings and roofs are all new
realizer rules. In exchange it has the best measured-data supply (the modular grid plus the national
cultural-property database).
