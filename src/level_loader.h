#pragma once

#include "mesh.h"
#include "vendor/HandmadeMath.h"
#include <string>
#include <vector>

// Named sub-mesh range within LevelData::mesh
struct SubMeshRange {
    char     name[64]     = {};
    uint32_t index_start  = 0;
    uint32_t index_count  = 0;
};

struct LevelData {
    Mesh mesh;
    HMM_Vec3 spawn_pos   = HMM_V3(0.0f, 1.0f, 15.0f);
    bool     has_spawn    = false;

    // Named sub-mesh ranges (e.g. "Mag" for weapon magazine)
    std::vector<SubMeshRange> submeshes;
};

// Load a .glb/.gltf file and extract all mesh geometry.
// If a node named "spawn" (case-insensitive) exists, its position is used as spawn.
// Returns empty mesh on failure.
LevelData load_level_gltf(const std::string& path);
