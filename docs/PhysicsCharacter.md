# PhysicsCharacter — DEPRECATED

> **This class was moved to `engine/deprecated/bullet/` when Bullet was first removed from active
> builds, then that whole `engine/deprecated/` tree (including `bullet/` and `active/`) was
> DELETED outright by commit `c8803a2` ("Remove Bullet Physics and dead/stale files"). The
> directory no longer exists in the working tree — the classes below are recoverable only from
> git history (pre-`c8803a2`), not present anywhere in the current source.**
>
> Bullet Physics has been removed from the Phyxel build. `PhysicsCharacter`, `SpiderCharacter`, `VoxelCharacter`, `Character`, and `PhysicsDriveMode` no longer exist in the tree — recoverable from git history only.
>
> For the current character system, see [AnimatedCharacter.md](AnimatedCharacter.md).

---

## Historical Notes

`PhysicsCharacter` was a fully physical **Active Ragdoll** character built on Bullet's `btRigidBody` + `btHingeConstraint` system. It used a PID controller for upright balance and motor-driven legs for locomotion.

### Why it was removed

- Bullet Physics introduced significant CPU overhead at scale and added friction to the build system (separate submodule compilation, headers across the codebase)
- `AnimatedVoxelCharacter` with `VoxelDynamicsWorld`-backed ragdoll parts provides the same character simulation without Bullet
- The `VoxelDynamicsWorld` custom engine is purpose-built for voxel debris and compound rigid bodies, replacing all Bullet CPU use cases

### Archived files (DELETED — git history only, pre-`c8803a2`)

| File | Last location before deletion |
|------|----------|
| `PhysicsCharacter.h/.cpp` | `engine/deprecated/bullet/` |
| `SpiderCharacter.h/.cpp` | `engine/deprecated/bullet/` |
| `VoxelCharacter.h/.cpp` | `engine/deprecated/bullet/` |
| `Character.h/.cpp` | `engine/deprecated/bullet/` |
| `PhysicsDriveMode.h/.cpp` | `engine/deprecated/bullet/` |
| `Player.h/.cpp` | `engine/deprecated/bullet/` |
| `Enemy.h/.cpp` | `engine/deprecated/bullet/` |
| `PhysicsCharacterTest` | `engine/deprecated/bullet/tests/` |
| `VoxelCharacterTest` | `engine/deprecated/bullet/tests/` |
| `PhysicsStressTests` | `engine/deprecated/bullet/tests/` |
