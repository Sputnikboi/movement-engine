#pragma once

#include "entity.h"
#include "mesh.h"
#include "renderer.h"
#include "vendor/HandmadeMath.h"

// ============================================================
//  Frustum for culling
// ============================================================

struct Frustum {
    HMM_Vec4 planes[6]; // left, right, bottom, top, near, far
                         // each plane: (nx, ny, nz, d) where nx*x + ny*y + nz*z + d >= 0 is inside

    // Extract from view-projection matrix
    void extract(const HMM_Mat4& vp);

    // Test if sphere is at least partially inside the frustum
    bool sphere_visible(HMM_Vec3 center, float radius) const;
};

// ============================================================
//  Entity rendering
// ============================================================

Mesh create_icosphere(int subdivisions = 1);

// Build a mesh of all alive entities visible to the frustum.
// knife_mesh: if non-null, player knife projectiles render as this mesh instead of a sphere.
Mesh build_entity_mesh(const Entity entities[], int max_entities,
                       const Frustum& frustum,
                       const Mesh* knife_mesh = nullptr);

// Build transparent blue shield bubbles around shielded enemies
void build_shield_bubbles(Mesh& transparent_out,
                          const Entity entities[], int max_entities,
                          const Frustum& frustum);

// Build turret laser effects:
//  - Thin red aiming laser (opaque, emissive) when tracking/windup/firing
//  - Thick railgun beam (transparent) when firing
//  - Charge-up particles orbiting turret during windup
struct CollisionWorld;  // forward decl
void build_turret_effects(Mesh& opaque_out, Mesh& transparent_out,
                          const Entity entities[], int max_entities,
                          const CollisionWorld& world,
                          const Frustum& frustum, float total_time);
