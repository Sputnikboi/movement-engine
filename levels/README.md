# Making Levels

## Blender Workflow

1. Open Blender
2. Model your level using standard mesh objects (cubes, planes, etc.)
3. Use **Object Mode** transforms (position, rotation, scale) — they're applied on export
4. Assign materials with base colors — the engine reads the PBR base color as vertex color
5. You can also use **Vertex Colors** for per-vertex coloring
6. Export: **File → Export → glTF 2.0 (.glb/.gltf)**
   - Format: **glTF Binary (.glb)** (single file, easier to manage)
   - Check **Apply Modifiers**
   - Check **+Y Up** (Blender default, matches the engine)
7. Run: `./movement_engine levels/your_level.glb`

## Important Notes

- The engine loads ALL meshes in the scene as collision geometry
- Player spawns at (0, 1, 15) by default — put a floor there
- Scale: 1 Blender unit = 1 meter. Player is 1.8m tall, can jump ~1.3m
- Keep normals consistent (Mesh → Normals → Recalculate Outside)
- Non-manifold geometry is fine — the engine uses triangle soup collision

## Quick Test with the Bundled Level

```bash
./movement_engine levels/test_arena.glb
```
