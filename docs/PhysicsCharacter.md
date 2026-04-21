# PhysicsCharacter — DEPRECATED

> **This class has been moved to `engine/deprecated/bullet/` and is no longer compiled or available in active builds.**
>
> Bullet Physics has been removed from the Phyxel build. `PhysicsCharacter`, `SpiderCharacter`, `VoxelCharacter`, `Character`, and `PhysicsDriveMode` are archived in `engine/deprecated/bullet/` for historical reference only.
>
> For the current character system, see [AnimatedCharacter.md](AnimatedCharacter.md).

---

## Historical Notes

`PhysicsCharacter` was a fully physical **Active Ragdoll** character built on Bullet's `btRigidBody` + `btHingeConstraint` system. It used a PID controller for upright balance and motor-driven legs for locomotion.

### Why it was removed

- Bullet Physics introduced significant CPU overhead at scale and added friction to the build system (separate submodule compilation, headers across the codebase)
- `AnimatedVoxelCharacter` with `VoxelDynamicsWorld`-backed ragdoll parts provides the same character simulation without Bullet
- The `VoxelDynamicsWorld` custom engine is purpose-built for voxel debris and compound rigid bodies, replacing all Bullet CPU use cases

### Archived files

| File | Location |
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
