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
