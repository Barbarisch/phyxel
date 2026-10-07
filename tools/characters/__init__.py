"""Roadmap R2 (docs/CharacterAnimationRoadmap.md): Meshy -> voxel character pipeline.

voxelize.py      model -> scaled shell voxels at the size-rule pitch, under the part budget
palette.py       per-voxel texture colour -> deterministic <= 24-colour palette
spec_complete.py forge species spec + R1 needs -> spec with the parts the stat block requires
binder.py        voxels + our skeleton (forge species or humanoid) -> fitted, bound .anim
manifest.py      resources/characters/<id>.json schema, validation, bindings writer
make_fixture_models.py  synthetic textured GLBs for offline tests (no Meshy credits)
"""
