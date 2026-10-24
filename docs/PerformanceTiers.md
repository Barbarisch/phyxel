# Performance Tiers — aesthetics first, then fit the tiers

> **Status (2026-10-24): STANDING PRINCIPLE (owner) + PLAN.** Every design in the engine states where it fits in the tiers
> below. The engine has **no quality-tier setting yet** (searched 2026-10-24: none in `engine/`); building one is item 1 of §5.
> Gate: [`FeatureDesignKeys.md`](FeatureDesignKeys.md) ("Where does it fit in the performance tiers?").

## 1. The owner's words (2026-10-24)

> "once we start dropping below 120 fps i get concerned. and i want to have rich worlds with rich visuals and asthetic. so i
> guess we just have do the best we can. i think we need to keep simulation in water to the smallest area possible and figure
> out how to cheaping render water and keep it all manageable."

> "i think working towards graphics tiers. so what we can accomplish at 30fps, 60fps, and 120fps, and then anything above. i
> think that all our designs should be figuring out where we fit into those"

> "we should be developing for asthetic first (maybe some thought into performance), but it seems best to me to have the
> moonshot quality looking stuff and then figure out optimizations after that to get it into our performance tiers. that way
> we are always trying to get to the best and only sacrificing when we need to."

## 2. The tiers

| Tier | Whole-frame time | Meaning |
|---|---|---|
| **30** | 33.3 ms | the floor: everything works and looks right, at reduced reach and detail |
| **60** | 16.7 ms | the standard tier |
| **120** | 8.3 ms | the owner's comfort line ("below 120 fps i get concerned") |
| **above** | < 8.3 ms | high-refresh: the least that still looks like the game |

A tier is a **whole-frame** target, on the **CPU frame and the GPU frame** each (the frame runs at the slower of the two). A
system's share of a tier is a proposal, measured and adjusted against everything else in the frame - no system owns a fixed
number. (The water's "2 ms for all water" of §25.1 / §0a of `WaterCore.md` was such a number, proposed by the agent and
accepted on 2026-10-11; it is superseded by this document.)

## 3. The order of work: aesthetics first

1. **Build the moonshot look.** The best version we can make, judged by the owner in motion. Performance is a thought, not a
   constraint, at this stage - but see rule 3 below.
2. **Measure what it costs** as soon as it exists, at a realistic play scene, whole frame (rule 4). This is the gap to each tier.
3. **Fit it into the tiers** by turning its scaling knobs down - the best look at the tier that can afford it, and a sacrifice
   only where a tier forces one. **Which tier gives up what is the owner's call** (a look decision).

## 4. The rules

1. **Tiers change cost and appearance, never the outcome.** Where water ends up, how much there is, where debris settles,
   what a character can walk on, what a structure is - identical at every tier. A game must not play differently on a slower
   machine. (For water: the one water model, mass conserved - a tier changes how much is SIMULATED and how it is DRAWN, never
   what water exists.)
2. **Tiers are a pure function of the setting and world position** - never of a chunk's identity (the chunk rule of
   `FeatureDesignKeys.md`). A tier's reach is a distance, not a chunk count.
3. **"Some thought into performance" means the moonshot can be scaled down without a rewrite.** The design keeps its
   scaling knobs from the start - *how much* is simulated, *how far* detail reaches, *how often* it updates - and keeps
   data where scaling it is cheap (on the GPU when the GPU uses it). The expensive mistake is a look that only exists if the
   whole of something runs at full detail.
4. **Report the whole frame, truthfully.** `engine_timing` cpuFrameTime, `gpu_timing` gpu_frame_ms, fps, plus the system's own
   CPU and GPU share - at a realistic play scene AND the system's worst case. Say what was not measured. Never call something
   "within budget" from one timer.
5. **A design states its tier table** (§4a) before it is built; the numbers are filled in when measured.

### 4a. The tier table every design carries

| | moonshot (measured cost) | 30 | 60 | 120 | above |
|---|---|---|---|---|---|
| knob 1 (e.g. simulated area) | | | | | |
| knob 2 (e.g. detail reach) | | | | | |
| what the player sees given up | - | | | | |
| measured CPU / GPU share | | | | | |

## 5. The plan

1. **The tier setting** - one engine-wide setting (30 / 60 / 120 / above) every system reads its knobs from, exposed over the
   API (with the resulting knob values echoed back) and in the settings UI. Design first (Feature Design Keys gate).
2. **Per system, as each is next worked on** - write its tier table: water first (`WaterCore.md` §30), then the systems with
   existing distance knobs (shadows `NearShadowCascade.md`, LOD tiers `LodTierLedger.md`, grass/foliage, characters).
3. **A realistic play scene per system** to measure in, beside its stress case (water: a player wading across a lake and
   throwing debris, beside the churning 60 m lake).
