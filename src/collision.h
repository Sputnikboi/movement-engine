#pragma once

#include "vendor/HandmadeMath.h"
#include "geo_types.h"
#include "mesh.h"
#include "bvh.h"
#include <vector>
#include <cstdint>

// Runtime toggle for collision logging (press F5)
extern bool g_collision_log;

// ============================================================
//  Collision world — holds the triangle soup for queries
// ============================================================

struct CollisionWorld {
    std::vector<Triangle> triangles;
    BVH bvh;

    // Ladder volumes (auto-computed from node geometry)
    struct LadderVolume {
        HMM_Vec3 mins;          // AABB min corner (inflated)
        HMM_Vec3 maxs;          // AABB max corner (inflated)
        HMM_Vec3 face_normal;   // average face normal (points away from wall)
    };
    std::vector<LadderVolume> ladder_volumes;
    float ladder_inflate = 0.1f;  // how much to inflate the AABB on each side

    // Build from a Mesh (extracts triangles from vertex/index data, builds BVH)
    void build_from_mesh(const Mesh& mesh);

    // Add extra triangles + rebuild BVH (for invisible collision like ladders)
    void add_mesh_triangles(const Mesh& mesh);

    // Add a ladder volume from a mesh node's geometry. Computes AABB + average normal,
    // inflates by ladder_inflate.
    void add_ladder_volume(const Mesh& mesh, uint32_t index_start, uint32_t index_count);

    // Check if a point is inside any ladder volume.
    // ignore_idx: skip this volume (used for jump-off cooldown, -1 = none).
    // Returns true + the ladder's face normal + volume center + volume index.
    bool on_ladder(HMM_Vec3 center, float radius,
                   HMM_Vec3& ladder_normal, HMM_Vec3& ladder_center,
                   int& volume_idx, int ignore_idx = -1) const;

    // Ray vs all triangles. Returns closest hit.
    HitResult raycast(HMM_Vec3 origin, HMM_Vec3 dir, float max_dist) const;

    // Find closest point on any triangle to a sphere.
    // Returns the deepest penetration (if any).
    // push_out = direction to move sphere to resolve overlap, length = penetration depth.
    bool sphere_overlap(HMM_Vec3 center, float radius,
                        HMM_Vec3& push_out, float& penetration) const;

    // Iterative depenetration: resolve ALL overlaps (not just deepest).
    // Pushes sphere out of geometry over multiple iterations.
    // Returns final resolved position.
    HMM_Vec3 depenetrate(HMM_Vec3 center, float radius, int max_iters = 8) const;

    // Move a sphere from `start` by `displacement`, sliding against geometry.
    // Returns the final position after up to 4 slide iterations.
    // `velocity` is modified in-place (clipped against contact planes).
    HMM_Vec3 slide_move(HMM_Vec3 start, float radius,
                        HMM_Vec3& velocity, float dt) const;

    // Step-move: try to step up over small obstacles (Source-style).
    // Returns true if step-up succeeded, writing the new position to `out_pos`.
    bool step_move(HMM_Vec3 start, float radius, float step_height,
                   HMM_Vec3 velocity, float dt, HMM_Vec3& out_pos) const;
};

// ============================================================
//  Low-level geometric primitives
// ============================================================

// Ray vs single triangle (Möller–Trumbore). Returns parametric t, or -1 on miss.
float ray_triangle(HMM_Vec3 origin, HMM_Vec3 dir,
                   HMM_Vec3 v0, HMM_Vec3 v1, HMM_Vec3 v2);

// Closest point on triangle to a given point.
HMM_Vec3 closest_point_on_triangle(HMM_Vec3 p, HMM_Vec3 v0, HMM_Vec3 v1, HMM_Vec3 v2);
