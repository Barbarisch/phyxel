# Sound System v2 — Biome Soundscapes, Music & the Audio Asset Library

> Status: **DESIGN / not yet started** (2026-07-09). This is the plan for giving the engine a
> real audio identity: a **library of biome-centric ambience + sound effects** (wind, insects,
> birds, creatures, water, cave drips) and **situational music** (busy medieval city theme,
> ethereal elven-forest theme), generated **AI-first where quality allows** and acquired from
> CC0 libraries where it doesn't. It covers both the **asset pipeline** (how sounds get made)
> and the **runtime** (how the engine picks and plays them). Companion to
> [`docs/TerrainGenerationV2.md`](TerrainGenerationV2.md) (biomes/climate are the ambience
> driver) — the biome→audio binding designed here must stay reconciled with the biome schema
> there.

---

## 0. The goal

When you stand in a savanna you hear dry wind and distant insects; step into deep forest and
birdsong and rustling canopy crossfade in; descend into a cave and it becomes drips and low
rumble; walk into a busy town and a lively theme rises over tavern chatter. All of it driven by
**data the engine already computes** (biome, depth, settlement proximity, time of day) — not
hand-scripted per world. The asset library behind it is **regenerable from manifests** (the same
philosophy as `tree_forge.py` / `fetch_cc0_textures.py`): prompts + sources in, packed and
provenance-tracked audio out.

---

## 1. What ships today (v1 ground truth)

Ground truth established by **source read only** (2026-07-09) — none of it has been
runtime-audited this session. States below marked "per source" are code-level findings; the
only *measured* facts are file-existence ones (4 WAVs on disk; `hit.wav` absent repo-wide).
**P0 must open with a runtime audit**: play a 2D + a 3D sound, move the camera, confirm
panning/attenuation audibly + via logs; run `scripts/audio_demo.py` (Doppler demo); exercise
`control_music` end-to-end. Anything that fails that audit becomes a P0 fix item.

| Layer | File | What it does | State |
|-------|------|--------------|-------|
| **Backend** | `engine/include/core/AudioSystem.h`, `engine/src/core/AudioSystem.cpp` | Single miniaudio wrapper (`MINIAUDIO_IMPLEMENTATION` at `AudioSystem.cpp:9`). WAV/MP3/FLAC (+ Vorbis via `stb_vorbis`). SFX/music stream from file; `preloadSound()` (`:238`) decodes to RAM. Sound pooling/recycling by path+channel. | Per source (2D `place.wav` path is the only one exercised in normal play) |
| **3D audio** | `AudioSystem.cpp:91` (`update`), `:146` (`playSound3D`) | Spatialization wired end-to-end per source: per-sound position/velocity, listener pos/dir/up/velocity pushed every frame from the camera (`Application.cpp:3728`). | Per source; not runtime-audited |
| **Buses** | `AudioSystem.cpp:63-65` | `ma_sound_group`s for SFX / Music / Voice; `setChannelVolume` (`:233`). | Per source; one bug found in code (§2.2) |
| **Music** | `engine/include/core/MusicPlaylist.h` (header-only) | Sequential/shuffle playlist, auto-advance on track end (`:57`, polled from `Application.cpp:3207`), full JSON state (`:75`). | Per source; **dormant** — only populated via manual `control_music add_track` calls; nothing loads it |
| **API/MCP** | `EngineAPIServer.cpp:3937-4259`, handlers `Application.cpp:1461, 5732, 11547, 11565` | `list_sounds`, `play_sound` (2D/3D), `set_volume`, `get_music_state`, `control_music`. Python bindings for play/preload (`Bindings.cpp:142-151`). | Works |
| **Assets** | `resources/sounds/` | **4 WAVs total**: `click.wav`, `place.wav`, `test.wav`, `whoosh.wav`. No catalog, no provenance file. | The actual gap |

Gameplay currently emits sound from exactly **two** call sites: voxel place → `place.wav`
(`VoxelInteractionSystem.cpp:314,323,332`) and furniture activation → `hit.wav`
(`VoxelInteractionSystem.cpp:572`) — **which does not exist on disk** (silent dead reference).
TTS plays on the Voice channel (`TTSService.cpp:294`). No break, footstep, combat, door,
ambience, or weather sounds anywhere.

---

## 2. Limitations that block biome soundscapes

Each evidenced in source:

1. **No crossfading anywhere.** `playMusic` hard-stops the old track (`ma_sound_uninit`) before
   starting the new — a gap, not a fade. Biome ambience transitions and music changes both need
   fades (miniaudio supports `ma_sound_set_fade_in_milliseconds` natively).
2. **"Master" channel is a lie.** `getGroup()` (`AudioSystem.cpp:39-46`) maps
   `AudioChannel::Master` to the **SFX group** — there is no true master bus. `set_volume
   channel:Master` silently only ducks SFX.
3. **No sound catalog.** `list_sounds` is a live directory scan (`Application.cpp:1461-1480`).
   No named events, no variation pools ("play one of 5 break-stone sounds"), no per-event
   volume/pitch ranges, no way to validate that referenced sounds exist (the `hit.wav` bug is
   exactly the failure a catalog check catches).
4. **No provenance file** — no analog to `resources/textures/source/CC0_SOURCES.json`. Violates
   the grounding discipline the moment we import CC0 assets.
5. **No biome→audio binding.** `resources/biomes.json` and the `Biome` struct
   (`WorldGenerator.h:85-119`) carry zero audio fields.
6. **Biome-at-player exists in C++ but not over the API.**
   `WorldGenerator::sampleSurface(x,z)` (`WorldGenerator.h:148`) returns
   `{surfaceY, temperature, moisture, continentalness, biomeIndex, surfaceMat}` and is reachable
   at runtime via `chunkManager->getStreamingGenerator()` — but `get_terrain_height`
   (`Application.cpp:12462`) ignores it, so nothing outside C++ can ask "what biome is here."
7. **Game definitions can't declare audio.** `GameDefinitionLoader` has no music/ambience/sound
   keys; no sample game definition mentions audio at all.
8. **`play_sound` hard-prefixes `resources/sounds/`** (`Application.cpp:11551`) — a library
   organized into subfolders (`ambience/`, `sfx/break/`, `music/`) works, but project-local
   `game_assets` sounds don't; `AssetManager::resolveSound` (`AssetManager.cpp:70`) exists for
   that and the handler should use it.

---

## 3. Acquisition landscape (researched 2026-07-09) — what we generate with, and what we must not touch

Web-researched with citations; licensing verdicts are the load-bearing part. **Everything below
feeds one rule: every shipped sound has a provenance row — generator+prompt, or source URL +
license.**

### 3.1 Local, open-weight (runs on the dev 4090 — free, unlimited iteration)

| Model | For | Key facts | Shippability |
|-------|-----|-----------|--------------|
| **Stable Audio 3 Small SFX** (May 2026) | One-shot SFX + ambience beds | ~0.6B, up to ~2 min clips, trained on **fully licensed data** (AudioSparx + Freesound); trivial on a 4090. ⚠ HF card ambiguity: config may be 16 kHz — **verify output sample rate empirically before adopting** (P1 gate). [Announcement](https://stability.ai/news-updates/meet-stable-audio-3-the-model-family-built-for-artistic-experimentation-with-open-weight-models) · [HF](https://huggingface.co/stabilityai/stable-audio-3-small-sfx) | ✅ Stability Community License: **outputs owned, commercial OK under $1M/yr revenue**; add a "Powered by Stability AI" credits line (cheap insurance vs an ambiguous attribution clause) |
| **Stable Audio 3 Medium** | Longer music/beds, provenance-conservative music option | Open-weight, 6+ min | ✅ same license |
| **Stable Audio Open 1.0** (2024) | 44.1 kHz-certain SFX backup | ≤47 s, 44.1 kHz stereo, <12 GB VRAM | ✅ same license |
| **ACE-Step 1.5 / XL** | **Music workhorse** — multi-minute instrumental themes | **Apache 2.0 weights**; base <4 GB VRAM, XL (4B) ~20 GB — 4090 fits; seconds per song; community benchmarks place it at/above hosted services. [GitHub](https://github.com/ace-step/ACE-Step-1.5) | ✅ Apache 2.0. ⚠ training-data provenance undisclosed (weights license ≠ data pedigree); output-side risk for instrumental fantasy themes is low |

### 3.2 Hosted APIs (use where clearly better)

- **ElevenLabs SFX v2** — the **only generator with native seamless-loop output** (`loop: true`),
  48 kHz, ≤30 s. Royalty-free commercial on paid plans (~$5–22/mo; a whole game's SFX list is a
  few dollars of credits). Use for loop beds local models keep missing.
  [Docs](https://elevenlabs.io/docs/overview/capabilities/sound-effects)
- **Eleven Music** — trained exclusively on licensed data; "cleared for commercial use incl.
  gaming" on paid plans (Enterprise carve-out for "large studio games" — we're not that). Option
  for hero tracks if licensed-training-data guarantee is wanted.
- **Google Lyria 3 (Vertex)** — IP-indemnified, legally safest hosted music, but GCP setup
  friction for a solo dev. Backup option only.
- The **Anthropic API key's role is prompt-writer/curator, not generator**: Claude authors the
  per-biome prompt manifests, names/tags outputs, and writes catalog descriptions.

### 3.3 CC0 / free libraries (mandatory fallback for AI-weak categories)

Practitioner consensus 2025–26: AI is **good** at wind/water/rain beds, cave rumble, whooshes,
impacts, UI, magic/fantasy (no reality anchor); AI is **bad** at realistic **birdsong, animal
vocalizations, and footstep cadence** — humans instantly hear those as wrong. Those three
categories come from libraries:

- **Freesound.org** — APIv2 with `license:"Creative Commons 0"` filter + official Python client
  → scripted bulk fetch, the direct analog of `fetch_cc0_textures.py`. CC0 = no attribution.
- **Sonniss GDC bundles** (2015–2026) — hundreds of GB of pro SFX, royalty-free unlimited
  commercial, no attribution. Bulk zips, no API. (License forbids reselling standalone or AI
  training on them — fine, we do neither.)
- **Kenney.nl** — CC0 UI/impact packs, instant UI baseline.

### 3.4 Hard exclusions (do not use, even as placeholders)

- **Meta AudioCraft (AudioGen/MusicGen)** — weights are **CC-BY-NC**, commercial prohibited.
- **TangoFlux** — research-only license.
- **BBC Sound Effects archive** — RemArc license is non-commercial.
- **Suno / Udio** — live UMG/Sony litigation (pivotal fair-use ruling expected summer 2026);
  Udio can't even export files post-settlement. Legally radioactive for shipped output.

---

## 4. Design

### 4.1 The sound catalog — `resources/sounds/sounds.json` (new)

Data-driven registry, same philosophy as `materials.json`. Gameplay code stops naming files and
starts naming **events**; the catalog maps events to variation pools:

```json
{
  "events": {
    "voxel.break.stone":  { "files": ["sfx/break/stone_01.ogg", "sfx/break/stone_02.ogg", "sfx/break/stone_03.ogg"],
                            "volume": [0.8, 1.0], "pitch": [0.92, 1.08], "channel": "SFX", "spatial": true },
    "ui.click":           { "files": ["sfx/ui/click.wav"], "channel": "SFX", "spatial": false },
    "furniture.activate": { "files": ["sfx/interact/thunk_01.ogg"], "spatial": true }
  }
}
```

- Random variation pick + pitch/volume jitter kills repetition fatigue.
- `SoundRegistry` (new, `engine/src/core/`) loads it; `playEvent(name, worldPos?)` is the one
  gameplay-facing call. Per-material families resolve by convention:
  `voxel.break.<materialSoundClass>` with a `soundClass` field added per material in
  `materials.json` (stone, wood, dirt, sand, glass, metal, foliage, cloth — ~8 classes cover 97
  materials).
- **Provenance:** `resources/sounds/SOURCES.json` — one row per shipped file: either
  `{generator, model, prompt, date}` or `{source_url, license, author}`. No row → CI-style
  validation failure.
- **Catalog validation is a unit test**: every file referenced by `sounds.json`, `biomes.json`
  ambience blocks, and every C++ `playEvent` literal exists on disk; every disk file has a
  SOURCES row. (This test, written first, goes red today on `hit.wav` — our free red-before-green.)

### 4.2 Biome ambience runtime — `AmbienceDirector` (new)

The core insight from research: **a soundscape = a loopable bed + randomized one-shot scatter**,
not one long recording. This is also exactly how we route around AI's birdsong weakness — the
bed is AI wind/rustle, the scattered chirps are CC0 recordings fired individually.

- **Bed layer**: per-biome looping ambience (2D, new `Ambience` bus). On biome change, equal-power
  crossfade old→new over ~2–4 s, with **hysteresis** (require the new biome sampled for ~3 s
  before switching — no thrash when strafing a border).
- **Scatter layer**: per-biome list of one-shot events, each with `intervalRange`, day/night
  weighting, and 3D placement at a random offset around the listener (birds above, insects at
  ground level) — this makes the world feel inhabited and directional for free, since 3D
  spatialization already works.
- **Context modifiers**, evaluated per tick from data the engine already has:
  - **Underground** (player Y far below `sampleSurface().surfaceY`): crossfade to the cave
    soundscape regardless of surface biome.
  - **Day/night** (`DayNightCycle`): scatter tables swap (birds → crickets/owls); bed can swap.
  - **Weather/settlement**: future hooks, same mechanism.
- **Biome binding** in `biomes.json`, parsed into the `Biome` struct by `loadBiomes()`:

```json
"ambience": {
  "bed":       { "day": "ambience/forest_day.ogg", "night": "ambience/forest_night.ogg" },
  "scatter":   [ { "event": "amb.bird.songbird", "interval": [4, 15], "when": "day" },
                 { "event": "amb.insect.cricket", "interval": [2, 8],  "when": "night" } ]
}
```

- Driver: `AmbienceDirector::update()` from the main loop, sampling
  `chunkManager->getStreamingGenerator()->sampleSurface(playerX, playerZ)` — no new world
  queries needed, the data already exists (§2.6).

### 4.3 Music director

`MusicPlaylist` stays as the low-level sequencer; a thin `MusicDirector` above it selects
**which playlist** by context: `menu`, `exploration/<biomeGroup>`, `settlement`, `combat`
(combat state already exists via `CombatSystem`). Rules: crossfade on context switch (fade the
old track out over ~2 s, fade the new in), deliberate silence gaps between exploration tracks
(constant music is fatiguing — beds carry the atmosphere), don't restart a context's playlist
from track 0 on every re-entry.

### 4.4 Game-definition audio block

`GameDefinitionLoader` gains an optional `"audio"` key: channel volumes, music playlists per
context, ambience overrides. This is how a packaged game ships its soundscape without code.

### 4.5 Engine fixes rolled in

- Real **Master** bus (fix `getGroup()`; route SFX/Music/Voice/Ambience groups through it).
- New **Ambience** channel/group (beds shouldn't duck with SFX).
- **Crossfade support** in `AudioSystem` via `ma_sound_set_fade_in_milliseconds` (+ a tiny
  fade-out-then-stop helper); `MusicPlaylist`/`MusicDirector` and `AmbienceDirector` both use it.
- `play_sound` handler resolves via `AssetManager::resolveSound` (project-overridable) instead
  of the hardcoded prefix; support subfolders.
- New `get_biome_at` MCP tool (or extend `get_terrain_height`) exposing `sampleSurface` fields —
  needed for agent-side verification of the ambience system, and useful far beyond audio.

### 4.6 Asset pipeline — `tools/audio_forge.py` + `tools/fetch_cc0_sounds.py` (new)

Forge-shaped, like everything else now: **manifest in → validated, packed, provenance-tracked
assets out.** Regenerable at any time.

```
tools/audio_manifest.json     # per-slot: type (bed|oneshot|music), prompt or source query,
                              # target duration, loop?, LUFS target, output path
tools/audio_forge.py          # local generation: SA3 Small SFX (sfx/beds), ACE-Step (music)
                              #   → N candidates per slot → post-process → pack
tools/fetch_cc0_sounds.py     # Freesound APIv2 CC0-filtered fetch (birds/animals/footsteps)
                              #   → same post-process → SOURCES.json rows with URL+license
```

Post-process stage (shared, Python: `soundfile`/`librosa`/`pyloudnorm`):
1. **Loop conditioning** for beds: cut at zero-crossings, equal-power crossfade tail→head
   (25–50 ms textures, up to seconds for evolving beds); **assert seam continuity
   programmatically** (RMS discontinuity across the wrap point below threshold) — that's the
   falsifiable loop test, not "sounded fine once."
2. **Loudness normalization** (grounded anchors: EBU R128 / Sony ASWG-R001 ≈ −23 LUFS overall
   mix): per-category targets — beds ~−29 LUFS, SFX ~−20, music masters ~−14 to −16 then
   attenuated by bus. Consistency within category is the invariant.
3. **Format pack**: **WAV 44.1 kHz/16-bit for short one-shots** (decode-free), **Ogg Vorbis q5–6
   for beds + music** (10× smaller, and unlike MP3 has no encoder padding → sample-accurate
   gapless loops; miniaudio decodes it via bundled `stb_vorbis`).
4. **Provenance row** written to `SOURCES.json` automatically — the tool is the only writer.

Curation stays human-in-the-loop: forge generates candidates cheaply (sub-second per SFX on the
4090), the user auditions and picks. Claude writes/varies the prompt manifest.

---

## 5. Phased plan

Foundation-first, same shape as terrain-v2. Each phase has its red test named up front
(red-before-green + solution-auditor per the standing discipline).

### P0 — Engine foundations (no assets yet)
**Step 0: runtime audit of the §1 claims** (2D/3D playback, panning/attenuation, Doppler demo,
`control_music` round-trip) — §1 is source-read only; anything that fails becomes a P0 item.
Then: `SoundRegistry` + `sounds.json` + catalog/provenance validation test (**red today**: `hit.wav`);
real Master bus + Ambience bus; crossfade helpers; `play_sound` via `resolveSound`; migrate the
3 existing gameplay call sites to `playEvent`; `get_biome_at` API.
**Red tests:** catalog test fails on `hit.wav`; Master-volume test fails on current
Master→SFX aliasing (set Master=0, assert Music group silent — fails now, passes after).
**Verify:** unit tests + live `set_volume`/`play_sound`/`get_biome_at` over MCP.

### P1 — Asset pipeline + starter library
Stand up SA3 Small SFX + ACE-Step locally (**gate: empirically verify SA3 output sample rate**;
if 16 kHz, fall back to Stable Audio Open 1.0 for one-shots and keep small-sfx for
rumbles/wind); build `audio_forge.py` + `fetch_cc0_sounds.py` + post-process stage; generate the
**starter set**: per-biome day/night beds for the existing biomes, scatter one-shots (CC0 birds/
insects), voxel break/place families for the ~8 material sound classes, UI set (Kenney), 2–3
music themes (exploration forest, settlement, menu) via ACE-Step.
**Red tests:** loop-seam assertion on a deliberately unconditioned bed (fails) → conditioned
(passes); LUFS-window assertion; catalog completeness.
**Stress:** batch-generate 50+ candidates unattended; assert every output passes post-process
gates (no silent clipping/truncation).

### P2 — Ambience runtime
`AmbienceDirector` (bed crossfade + hysteresis + scatter + underground + day/night),
`biomes.json` ambience blocks, wired to `sampleSurface`.
**Red test:** scripted walk across a biome border via MCP — assert (new `get_audio_state`
debug endpoint) exactly one crossfade fires, correct target bed, and no bed flapping while
strafing the border for 60 s (fails without hysteresis). Underground: teleport into a cave,
assert cave bed active despite surface biome.
**Stress:** teleport across 20 biome borders in 20 s (no leak/thrash — pool stats flat); 3-hour
idle soak (sound-handle count flat); scatter with 100 concurrent 3D one-shots (pool recycles,
no crash).

### P3 — Music director + game definitions
`MusicDirector` contexts (menu/exploration/settlement/combat), crossfades, silence gaps;
`"audio"` block in `GameDefinitionLoader`; a sample game definition exercising it.
**Red test:** enter combat via MCP → assert music context switches with fade (state via
`get_music_state`), and reverts after; load a game definition with an `audio` block → assert
playlists populated (fails before loader support).

### P4 — Gameplay SFX coverage
Break sounds (per material class — the break path currently emits **nothing**), footsteps
(CC0 sets, per-material, cadence from the animation FSM — explicitly *not* AI-generated),
combat hits, doors, ambient furniture (tavern fireplace crackle as a placed 3D looping emitter —
first structure-gen tie-in).
**Stress:** break 1,000 voxels rapidly (pool exhaustion behavior graceful); footstep audit at
walk/run/crouch speeds.

Deferred / future: weather audio (needs weather), settlement crowd-walla tied to NPC density,
reverb zones (miniaudio has no built-in reverb — needs a custom DSP node; don't promise it),
occlusion/obstruction, per-scene audio in multi-scene games.

---

## 6. Decision forks (user)

1. **Generation stack** — recommended **hybrid, local-first**: SA3 Small SFX + ACE-Step on the
   4090 (free, unlimited), ElevenLabs SFX v2 (~$22/mo Creator, cancellable) only for loops local
   models keep missing, CC0 libraries for birds/animals/footsteps. Alternatives: pure-local
   (zero cost, no native-loop generator) or API-first (best loops, ongoing cost).
2. **Music model** — recommended **ACE-Step 1.5/XL** (Apache 2.0, multi-minute, near-instant on
   the 4090). Conservative alternative: SA3 Medium (weaker music, but fully-licensed training
   data); premium alternative: Eleven Music for hero tracks.
3. **Stability attribution** — recommended: add "Powered by Stability AI" to game credits (the
   license's attribution clause is ambiguous for outputs-only shipping; a credits line is free).
   Also note the **$1M/yr revenue cliff** on the Community License (enterprise license needed
   beyond it — a good problem).
4. **Sequencing vs current workstreams** — this plan is independent of terrain-v2 P-increments
   (it consumes the biome system read-only). P0+P1 are the natural first slice; P1's biome bed
   list should track whatever biome set terrain-v2 lands on.
