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
Mesh build_entity_mesh(const Entity entities[], int max_entities,
                       const Frustum& frustum);

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

// Append billboard health bars above enemies to an existing mesh.
// cam_right/cam_up used for billboarding. Only shows bars for
// enemies that have taken damage (hp < max_hp) and are not dying.
void build_health_bars(Mesh& out, const Entity entities[], int max_entities,
                       const Frustum& frustum,
                       HMM_Vec3 cam_right, HMM_Vec3 cam_up);

// Emissive quad for particle/trail effects (normal.x = alpha, double-sided)
void append_emissive_quad(Mesh& out,
                          HMM_Vec3 a, HMM_Vec3 b, HMM_Vec3 c, HMM_Vec3 d,
                          float r, float g, float bl, float alpha);
