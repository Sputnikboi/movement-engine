#pragma once

#include "mesh.h"
#include <string>

// Load a .glb/.gltf file and extract all mesh geometry into a single Mesh.
// All meshes in the scene are merged into one (world geometry).
// Vertex colors are used if present, otherwise a default color is assigned.
// Returns an empty mesh on failure.
Mesh load_level_gltf(const std::string& path);
