# TODO List

## High Priority

### Face Detection Accuracy
- [ ] Fix raytracing face detection inaccuracy
  - Current issue: Face detection can be off when cursor is near cube edges/corners
  - The raycaster sometimes reports the wrong face, especially at acute angles
  - Need to improve the DDA algorithm's face normal calculation
  - Consider implementing sub-voxel precision for more accurate hit detection

### Chunk Loading Optimization
- [ ] Limit number of chunks loaded from database on startup
  - Current behavior: Loads ALL chunks from database regardless of world size
  - Could cause performance issues with large worlds (thousands of chunks)
  - Implement chunk streaming based on player position
  - Only load chunks within a certain radius of spawn point initially
  - Dynamically load/unload chunks as player moves around

### Chunk Creation Validation
- [ ] Check world database before creating new chunks
  - Before auto-creating empty chunks for player placement, verify they don't already exist in database
  - Prevents potential data loss if a chunk exists but isn't currently loaded
  - Add chunk existence check to `ensureChunkExists()` or `VoxelManipulationSystem::placeCube()`

## Medium Priority

### Database Performance
- [ ] Add database indexes for frequently queried chunk coordinates
- [ ] Implement chunk caching to reduce database queries
- [ ] Consider using prepared statements pooling for better performance

### World Management
- [ ] Add UI for creating/loading/deleting different worlds
- [ ] Implement world backup functionality
- [ ] Add chunk compression for database storage

## Low Priority

### Code Cleanup
- [ ] Remove excessive face detection logging (currently logs every frame)
- [ ] Clean up debug output verbosity settings
- [ ] Document the chunk persistence architecture

### Testing
- [ ] Add unit tests for chunk loading/saving
- [ ] Add integration tests for world persistence
- [ ] Test with very large worlds (1000+ chunks)
