#pragma once

#include "vendor/HandmadeMath.h"
#include "mesh.h"
#include <vector>
#include <cstdint>

// ============================================================
//  Triangle stored for collision queries
// ============================================================

struct Triangle {
    HMM_Vec3 v0, v1, v2;
    HMM_Vec3 normal;
};

// ============================================================
//  Hit result from a ray or sweep
// ============================================================

struct HitResult {
    bool     hit      = false;
    float    t        = 1e30f;      // parametric distance along ray
    HMM_Vec3 point    = {};         // world-space hit point
    HMM_Vec3 normal   = {};         // surface normal at hit
};

// ============================================================
//  Collision world — holds the triangle soup for queries
// ============================================================

struct CollisionWorld {
    std::vector<Triangle> triangles;

    // Build from a Mesh (extracts triangles from vertex/index data)
    void build_from_mesh(const Mesh& mesh);

    // Ray vs all triangles. Returns closest hit.
    HitResult raycast(HMM_Vec3 origin, HMM_Vec3 dir, float max_dist) const;

    // Find closest point on any triangle to a sphere.
    // Returns the deepest penetration (if any).
    // push_out = direction to move sphere to resolve overlap, length = penetration depth.
    bool sphere_overlap(HMM_Vec3 center, float radius,
                        HMM_Vec3& push_out, float& penetration) const;

    // Move a sphere from `start` by `displacement`, sliding against geometry.
    // Returns the final position after up to 4 slide iterations.
    // `velocity` is modified in-place (clipped against contact planes).
    HMM_Vec3 slide_move(HMM_Vec3 start, float radius,
                        HMM_Vec3& velocity, float dt) const;
};

// ============================================================
//  Low-level geometric primitives
// ============================================================

// Ray vs single triangle (Möller–Trumbore). Returns parametric t, or -1 on miss.
float ray_triangle(HMM_Vec3 origin, HMM_Vec3 dir,
                   HMM_Vec3 v0, HMM_Vec3 v1, HMM_Vec3 v2);

// Closest point on triangle to a given point.
HMM_Vec3 closest_point_on_triangle(HMM_Vec3 p, HMM_Vec3 v0, HMM_Vec3 v1, HMM_Vec3 v2);
