# Humanoid Animation Migration

This document is the review ledger for replacing combat and gathering motion in
`resources/animated_characters/humanoid.anim`. A candidate does not replace a
production clip until its complete source motion has been reviewed in the
animation editor after a full engine restart.

Distinct motion variants are intentional content, even when they represent the
same gameplay action. Remove exact motion duplicates and visually rejected
clips; retain approved variations so repeated actions do not look mechanical.

## Review states

- `SEARCH`: Find candidate motions.
- `DOWNLOADED`: Preserve the original FBX in `resources/mixamo_imports/`.
- `IMPORTED`: Import under a `review_*` name; do not wire into gameplay.
- `SOURCE_APPROVED`: The complete motion is visually acceptable.
- `CLIPS_APPROVED`: Crops, timing, contacts, and transitions are acceptable.
- `WIRED`: The animation family is mapped to gameplay and tested.
- `RETIRED`: Superseded clips have no live references and may be removed.

## Family backlog

| Order | Family | Weapons/actions | Minimum production clips | State |
| --- | --- | --- | --- | --- |
| 1 | `slash_2h` | greatsword, two-handed axe | 5 light attacks, heavy, 3 blocks, idle | WIRED |
| 2 | `blunt_2h` | maul, greatclub | 4 light attacks, heavy; provisional block | WIRED |
| 3 | `dagger_1h` | dagger | 3 light attacks, heavy/lunge, evade/parry | WIRED |
| 4 | `bow` | shortbow, longbow | ready, draw, aim loop, release, recover | CLIPS_APPROVED |
| 5 | `gather_mining` | pickaxe | ready, swing/impact, loop/recover | CLIPS_APPROVED |
| 6 | `gather_chopping` | wood axe | ready, swing/impact, loop/recover | CLIPS_APPROVED |
| 7 | `thrust_1h` | rapier, shortsword | thrust chain, slash, heavy lunge, parry | SOURCE_BLOCKED |
| 8 | `blunt_1h` | club, mace, warhammer | 4 light attacks; provisional heavy/block | WIRED |
| 9 | `staff_2h` | quarterstaff | 3 attacks, heavy, guard | SOURCE_BLOCKED |
| 10 | `spear` | spear, polearms | thrust chain, sweep, guard | SOURCE_BLOCKED |

`slash_1h` is already provisionally wired from `One Hand Sword Combo.fbx`.
Its split clips remain subject to transition cleanup after the Motion Bricks
work can blend into and out of non-neutral poses.

## Candidate naming and import rules

Use names that cannot accidentally become live gameplay clips:

```text
review_<family>_<source-slug>_<number>
```

Keep the uncut source motion until all derived clips have been approved. Record
the Mixamo title exactly because FBX filenames are the only practical source
provenance available after download.

## Batch 1: two-handed slashing

Search Mixamo for:

- `Two Handed Sword`
- `Great Sword Slash`
- `Great Sword Attack`
- `Sword And Shield Slash` (only if the off-hand motion can be discarded)
- `Standing Melee Attack`
- `Sword Combo`

Reject a candidate before download if it has locked elbows, hands intersecting
the torso, implausible grip spacing, excessive root travel, or no usable
anticipation/recovery. Prefer a complete combo for coherent transitions, but
also collect a clean single heavy attack and a guard/block if available.

### Candidate ledger

| Candidate | Exact Mixamo title | Source file | Intended role | State | Review notes |
| --- | --- | --- | --- | --- | --- |
| `slash2h_light1` | great sword slash | `great sword slash.fbx` | light 1 | WIRED | 1.27s |
| `slash2h_light2` | great sword slash (2) | `great sword slash (2).fbx` | light 2 | WIRED | 3.53s; root motion |
| `slash2h_light3` | great sword slash (3) | `great sword slash (3).fbx` | light 3 | WIRED | 1.83s |
| `slash2h_light4` | great sword slash (4) | `great sword slash (4).fbx` | light 4 | WIRED | 1.80s; root motion |
| `slash2h_light5` | great sword slash (5) | `great sword slash (5).fbx` | light 5 | WIRED | 1.43s |
| `slash2h_heavy` | great sword attack | `great sword attack.fbx` | heavy | WIRED | 1.20s |
| `slash2h_block1` | great sword blocking | `great sword blocking.fbx` | alternate block | CLIPS_APPROVED | 0.50s; root motion |
| `slash2h_block2` | great sword blocking (2) | `great sword blocking (2).fbx` | gameplay block | WIRED | 0.97s |
| `slash2h_block3` | great sword blocking (3) | `great sword blocking (3).fbx` | alternate block | CLIPS_APPROVED | 0.50s |
| `slash2h_idle` | great sword idle | `great sword idle.fbx` | ready/idle | CLIPS_APPROVED | 2.00s |

## Batch 2: two-handed blunt

Mixamo yielded one plausible result. The complete source is retained rather
than prematurely cropped.

| Candidate | Exact Mixamo title | Source file | Intended role | State | Review notes |
| --- | --- | --- | --- | --- | --- |
| `blunt2h_heavy` | Heavy Weapon Swing | `Heavy Weapon Swing.fbx` | heavy | WIRED | 5.20s full take; primary impact near 50% |

## Batch 3: daggers

| Candidate | Exact Mixamo title | Source file | Intended role | State | Review notes |
| --- | --- | --- | --- | --- | --- |
| `dagger1h_light1` | Stabbing | `Stabbing.fbx` | light 1 | WIRED | 2.13s; 44 animated channels |
| `dagger1h_light2` | Stabbing (1) | `Stabbing (1).fbx` | light 2 | WIRED | 2.37s; 52 animated channels |
| `dagger1h_light3` | Stabbing (2) | `Stabbing (2).fbx` | light 3 and provisional heavy | WIRED | 2.63s; 52 animated channels |

## Batch 4: bow and arrow

| Candidate | Exact Mixamo title | Intended role | State | Review notes |
| --- | --- | --- | --- | --- |
| `bow_draw` | standing draw arrow | draw/nock | CLIPS_APPROVED | 1.03s |
| `bow_idle` | standing idle 01 | ready/idle | CLIPS_APPROVED | 5.10s |
| `bow_aim_overdraw` | standing aim overdraw | draw/aim source | CLIPS_APPROVED | 3.77s |
| `bow_release_recoil` | standing aim recoil | release/recover | CLIPS_APPROVED | 0.70s |
| `bow_aim_walk_back` | standing aim walk back | aimed locomotion | CLIPS_APPROVED | 1.47s; root motion |
| `bow_aim_walk_forward` | standing aim walk forward | aimed locomotion | CLIPS_APPROVED | 1.20s; root motion |
| `bow_aim_walk_left` | standing aim walk left | aimed locomotion | CLIPS_APPROVED | 1.20s; root motion |
| `bow_aim_walk_right` | standing aim walk right | aimed locomotion | CLIPS_APPROVED | 1.30s; root motion |

Bow gameplay remains intentionally unwired until the ranged action state can
sequence draw, held aim, projectile release, and recovery without passing
through the melee combo mapper.

## Batch 5: mining

Mixamo did not expose a purpose-named mining or pickaxe motion. The preserved
`Standing Melee Attack Downward` source is being evaluated as a tool swing
without replacing its legacy combat import.

| Candidate | Exact Mixamo title | Intended role | State | Review notes |
| --- | --- | --- | --- | --- |
| `mining_wall_strike` | standing melee attack downward (Pro Melee Axe Pack) | wall mining strike | CLIPS_APPROVED | 2.27s; duplicate candidate removed |
| `digging_ground` | Digging | ground digging loop | CLIPS_APPROVED | 4.83s full take |

Gathering gameplay remains unwired until a resource-action state can apply
progress, sound, and particles at `impactFrameFraction`.

## Batch 6: wood chopping

| Candidate | Exact Mixamo title | Intended role | State | Review notes |
| --- | --- | --- | --- | --- |
| `woodchop_horizontal` | standing melee attack horizontal | forward tree strike | CLIPS_APPROVED | 2.40s |
| rejected | standing melee attack backhand | axe combat source | RETIRED | Removed from humanoid; source FBX retained |

The pack's high and low 360-degree attacks are retained as source material for
axe combat, but excluded from gathering review because they turn away from a
fixed resource target.

## Batch 7: one-handed thrusting

Mixamo searches for rapier, fencing, sword thrust, and lunge produced no usable
weapon motions. Keep the current piercing fallback until another source is
available; do not substitute dagger or club motion.

## Batch 8: blunt weapon combos

| Candidate | Exact Mixamo title | Intended role | State | Review notes |
| --- | --- | --- | --- | --- |
| `blunt1h_mocap_combo` | One Hand Club Combo | one-handed blunt combo source | SOURCE_APPROVED | 3.63s; split candidates not yet wired |
| `blunt2h_mocap_combo` | Two Hand Club Combo | two-handed blunt light-chain source | SOURCE_APPROVED | 5.43s; split candidates not yet wired |

Production cuts derived at measured recovery valleys (full sources retained):

- `blunt1h_light1` through `blunt1h_light4`
- `blunt2h_light1` through `blunt2h_light4`

All eight cuts were visually approved and are wired into their families.

## Legacy replacement: one-handed axe

This batch targets the generated `chop_1h` fallback after the core family
acquisition pass.

| Candidate | Source motion | State | Review notes |
| --- | --- | --- | --- |
| `axe1h_uppercut` | standing melee attack backhand | SOURCE_APPROVED | 3.17s; aggressive uppercut-style attack |
| `axe1h_spin_high` | standing melee attack 360 high | SOURCE_APPROVED | 3.17s |
| `axe1h_spin_low` | standing melee attack 360 low | SOURCE_APPROVED | 2.50s |
| `axe2h_mocap_combo1` | standing melee combo attack ver. 1 | SOURCE_APPROVED | 4.67s; root motion; visually classified as two-handed |
| `axe1h_mocap_combo2` | standing melee combo attack ver. 2 | SOURCE_APPROVED | 4.20s |
| `axe1h_mocap_combo3` | standing melee combo attack ver. 3 | SOURCE_APPROVED | 2.73s |

## Spell-casting expansion

The Magic Spell Pack and Lite Magic Pack are preserved intact. Thirteen unique
review motions were imported: three one-handed magic attacks, one one-handed
cast, one two-handed cast, two area attacks, five two-handed attacks, and one
alternate magic idle. The twelve casting motions were approved and promoted;
the alternate idle was rejected and removed.

Review names use the `review_cast_main_*` prefix plus
`review_cast_lite_idle2`. Exact retarget comparison removed these redundant
Lite-pack copies:

- `review_cast_lite_1h_attack1`
- `review_cast_lite_2h_area2`

No existing `cast_*` production clip is replaced until the new motions are
visually classified by spell role.

## Existing combat cleanup queue

These are replacement candidates, not yet safe to delete:

| Clip | Current live dependency | Retirement condition |
| --- | --- | --- |
| `melee_chop_2h` | `two_handed` family | `slash_2h` and `blunt_2h` are wired |
| `melee_sweep_2h` | `two_handed` and `spear` families | both families stop referencing it |
| `melee_stab_1h` | `stab_1h` family | dagger and thrust-sword families are wired |
| `melee_thrust_spear` | `spear` family | new spear set is wired |
| `melee_parry` | one-handed families | approved family-specific defense exists |
| `melee_attack_horizontal` | legacy item definitions | all item references are migrated |
| `melee_attack_down` | legacy item definitions | all item references are migrated |

Before retirement, scan code, JSON, tests, metadata, generated character files,
and animation names. Remove stale generated copies through the generation path
rather than hand-editing every character animation file.

### Confirmed cleanup

- `sword1h_guard`: rejected, no live references; removed from all character anims.
- `sword1h_prototype`: rejected, no live references; removed from all character anims.
- Remaining legacy melee clips are still referenced and are not safe to delete.

## Per-batch review gate

1. Download up to three strong candidates.
2. Preserve their source FBXs.
3. Import full takes under `review_*` names.
4. Fully restart the animation editor.
5. Review complete motions together.
6. Split only the approved source.
7. Review clips and transitions after another full restart.
8. Wire the new family and test timing in combat/resource gathering.
9. Retire old clips only after a zero-live-reference audit.
