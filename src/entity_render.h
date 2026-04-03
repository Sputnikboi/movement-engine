#pragma once

#include "entity.h"
#include "mesh.h"
#include "renderer.h"

// ============================================================
//  Entity rendering — generates a mesh each frame for all
//  alive entities (drones as icospheres, projectiles as small
//  bright spheres). Quick and dirty, no instancing yet.
// ============================================================

// Create the base icosphere mesh (unit radius, centered at origin).
// Call once at startup.
Mesh create_icosphere(int subdivisions = 1);

// Build a mesh of all alive entities for this frame.
// Drones rendered as colored spheres, projectiles as small bright spheres.
Mesh build_entity_mesh(const Entity entities[], int max_entities);
