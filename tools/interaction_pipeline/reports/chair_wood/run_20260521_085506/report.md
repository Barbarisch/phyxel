# Interaction matrix report: chair_wood / sit

- **Schema**: `matrix_result.v1`
- **Asset**: `G:\Github\phyxel\resources\templates\chair_wood.voxel`
- **Interaction point**: `seat_0`
- **Run window**: 2026-05-21T08:55:06 to 2026-05-21T08:56:19
- **Report generated**: 2026-05-21T08:56:19

## Summary

| Character | H (m) | hipW (m) | legL (m) | Issues | Can interact? |
|-----------|-------|----------|----------|--------|---------------|
| standard | 1.712 | 0.200 | 0.919 | 1 | yes |
| giant | 2.679 | 0.292 | 1.646 | 1 | yes |
| dwarf | 0.818 | 0.090 | 0.258 | 2 | yes |
| child | 1.012 | 0.111 | 0.352 | 2 | yes |

## standard

**Preset.** height=1.0, bulk=1.0, leg=1.0, torso=1.0, shoulder=1.0

**Metrics.** total_height=1.712 m, hip_height=0.919 m, leg_length=0.919 m, hip_width=0.200 m, shoulder_width=0.650 m, sitting_height=0.892 m

**Derived offsets.**

| Clip | Offset (x, y, z) |
|------|------------------|
| `sit_down` | (0.333, 0.667, 0.433) |
| `sitting_idle` | (0.333, 0.667, 0.333) |
| `sit_stand_up` | (0.333, 0.667, 0.433) |

**Compatibility issues.**

| Severity | Rule | Message |
|----------|------|---------|
| warn | `BACKREST_BLOCKS_VIEW` | Backrest 1.333m exceeds seated eye height ~0.792m |

**Screenshots.**

| Keypose | east | iso | south |
|---------|---|---|---|
| seated | ![standard seated east](images\standard_seated_east.png) | ![standard seated iso](images\standard_seated_iso.png) | ![standard seated south](images\standard_seated_south.png) |
| idle | ![standard idle east](images\standard_idle_east.png) | ![standard idle iso](images\standard_idle_iso.png) | ![standard idle south](images\standard_idle_south.png) |
| rising | ![standard rising east](images\standard_rising_east.png) | ![standard rising iso](images\standard_rising_iso.png) | ![standard rising south](images\standard_rising_south.png) |

## giant

**Preset.** height=1.25, bulk=1.3, leg=1.25, torso=1.2, shoulder=1.2

**Metrics.** total_height=2.679 m, hip_height=1.646 m, leg_length=1.646 m, hip_width=0.292 m, shoulder_width=0.948 m, sitting_height=1.159 m

**Derived offsets.**

| Clip | Offset (x, y, z) |
|------|------------------|
| `sit_down` | (0.333, 0.667, 0.433) |
| `sitting_idle` | (0.333, 0.667, 0.333) |
| `sit_stand_up` | (0.333, 0.667, 0.433) |

**Compatibility issues.**

| Severity | Rule | Message |
|----------|------|---------|
| warn | `BACKREST_BLOCKS_VIEW` | Backrest 1.333m exceeds seated eye height ~1.059m |

**Screenshots.**

| Keypose | east | iso | south |
|---------|---|---|---|
| seated | ![giant seated east](images\giant_seated_east.png) | ![giant seated iso](images\giant_seated_iso.png) | ![giant seated south](images\giant_seated_south.png) |
| idle | ![giant idle east](images\giant_idle_east.png) | ![giant idle iso](images\giant_idle_iso.png) | ![giant idle south](images\giant_idle_south.png) |
| rising | ![giant rising east](images\giant_rising_east.png) | ![giant rising iso](images\giant_rising_iso.png) | ![giant rising south](images\giant_rising_south.png) |

## dwarf

**Preset.** height=0.6, bulk=1.5, leg=0.55, torso=0.7, shoulder=1.1

**Metrics.** total_height=0.818 m, hip_height=0.258 m, leg_length=0.258 m, hip_width=0.090 m, shoulder_width=0.436 m, sitting_height=0.658 m

**Derived offsets.**

| Clip | Offset (x, y, z) |
|------|------------------|
| `sit_down` | (0.333, 0.667, 0.433) |
| `sitting_idle` | (0.333, 0.667, 0.333) |
| `sit_stand_up` | (0.333, 0.667, 0.433) |

**Compatibility issues.**

| Severity | Rule | Message |
|----------|------|---------|
| warn | `SEAT_TOO_TALL` | Seat 0.667m above floor; legs only 0.258m (feet dangle 0.409m, max 0.20m) |
| warn | `BACKREST_BLOCKS_VIEW` | Backrest 1.333m exceeds seated eye height ~0.558m |

**Screenshots.**

| Keypose | east | iso | south |
|---------|---|---|---|
| seated | ![dwarf seated east](images\dwarf_seated_east.png) | ![dwarf seated iso](images\dwarf_seated_iso.png) | ![dwarf seated south](images\dwarf_seated_south.png) |
| idle | ![dwarf idle east](images\dwarf_idle_east.png) | ![dwarf idle iso](images\dwarf_idle_iso.png) | ![dwarf idle south](images\dwarf_idle_south.png) |
| rising | ![dwarf rising east](images\dwarf_rising_east.png) | ![dwarf rising iso](images\dwarf_rising_iso.png) | ![dwarf rising south](images\dwarf_rising_south.png) |

## child

**Preset.** height=0.7, bulk=0.75, leg=0.65, torso=0.8, shoulder=1.0

**Metrics.** total_height=1.012 m, hip_height=0.352 m, leg_length=0.352 m, hip_width=0.111 m, shoulder_width=0.482 m, sitting_height=0.757 m

**Derived offsets.**

| Clip | Offset (x, y, z) |
|------|------------------|
| `sit_down` | (0.333, 0.667, 0.433) |
| `sitting_idle` | (0.333, 0.667, 0.333) |
| `sit_stand_up` | (0.333, 0.667, 0.433) |

**Compatibility issues.**

| Severity | Rule | Message |
|----------|------|---------|
| warn | `SEAT_TOO_TALL` | Seat 0.667m above floor; legs only 0.352m (feet dangle 0.315m, max 0.20m) |
| warn | `BACKREST_BLOCKS_VIEW` | Backrest 1.333m exceeds seated eye height ~0.657m |

**Screenshots.**

| Keypose | east | iso | south |
|---------|---|---|---|
| seated | ![child seated east](images\child_seated_east.png) | ![child seated iso](images\child_seated_iso.png) | ![child seated south](images\child_seated_south.png) |
| idle | ![child idle east](images\child_idle_east.png) | ![child idle iso](images\child_idle_iso.png) | ![child idle south](images\child_idle_south.png) |
| rising | ![child rising east](images\child_rising_east.png) | ![child rising iso](images\child_rising_iso.png) | ![child rising south](images\child_rising_south.png) |
