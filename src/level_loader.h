#pragma once

#include "mesh.h"
#include "entity.h"
#include "vendor/HandmadeMath.h"
#include <string>
#include <vector>

// Named sub-mesh range within LevelData::mesh
struct SubMeshRange {
    char     name[64]     = {};
    uint32_t index_start  = 0;
    uint32_t index_count  = 0;
};

// Enemy spawn point from Blender empties
struct EnemySpawn {
    HMM_Vec3 position;
    EntityType type;  // Drone or Rusher
};

struct LevelData {
    Mesh mesh;                // visible + collidable geometry
    Mesh ladder_mesh;         // ladder trigger geometry (invisible, not rendered)
    Mesh visual_only_mesh;    // VLadder etc: rendered but no collision
    HMM_Vec3 spawn_pos   = HMM_V3(0.0f, 1.0f, 15.0f);
    bool     has_spawn    = false;

    // Named sub-mesh ranges (reference mesh or ladder_mesh)
    std::vector<SubMeshRange> submeshes;
    std::vector<SubMeshRange> ladder_submeshes; // ranges into ladder_mesh

    // Enemy spawn points
    std::vector<EnemySpawn> enemy_spawns;
};

// Load a .glb/.gltf file and extract all mesh geometry.
// If a node named "spawn" (case-insensitive) exists, its position is used as spawn.
// Returns empty mesh on failure.
LevelData load_level_gltf(const std::string& path);
