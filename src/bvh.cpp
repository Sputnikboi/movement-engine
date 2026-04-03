#include "bvh.h"
#include "collision.h"
#include <algorithm>
#include <cstdio>
#include <cmath>

AABB AABB::from_triangle(const Triangle& tri) {
    AABB box = empty();
    box.expand(tri.v0);
    box.expand(tri.v1);
    box.expand(tri.v2);
    return box;
}

// ============================================================
//  Build BVH
// ============================================================

static constexpr uint32_t MAX_LEAF_TRIS = 4;

void BVH::build(const std::vector<Triangle>& triangles) {
    nodes.clear();
    tri_indices.clear();

    if (triangles.empty()) return;

    uint32_t count = static_cast<uint32_t>(triangles.size());

    // Initialize triangle indices (identity permutation)
    tri_indices.resize(count);
    for (uint32_t i = 0; i < count; i++)
        tri_indices[i] = i;

    // Precompute per-triangle AABBs
    std::vector<AABB> tri_bounds(count);
    for (uint32_t i = 0; i < count; i++)
        tri_bounds[i] = AABB::from_triangle(triangles[i]);

    // Reserve rough estimate of nodes (2*n - 1 for a complete binary tree)
    nodes.reserve(2 * count);

    build_recursive(triangles, tri_bounds, 0, count);

    fprintf(stdout, "BVH: %zu nodes for %u triangles\n", nodes.size(), count);
}

uint32_t BVH::build_recursive(const std::vector<Triangle>& triangles,
                               std::vector<AABB>& tri_bounds,
                               uint32_t start, uint32_t count)
{
    // Compute bounds for this set of triangles
    AABB bounds = AABB::empty();
    for (uint32_t i = start; i < start + count; i++)
        bounds.expand(tri_bounds[tri_indices[i]]);

    uint32_t node_idx = static_cast<uint32_t>(nodes.size());
    nodes.push_back({});
    BVHNode& node = nodes[node_idx];
    node.bounds = bounds;

    // Leaf node
    if (count <= MAX_LEAF_TRIS) {
        node.left = 0;
        node.right = 0;
        node.tri_start = start;
        node.tri_count = count;
        return node_idx;
    }

    // Find the longest axis to split on
    HMM_Vec3 extent = HMM_SubV3(bounds.max, bounds.min);
    int axis = 0;
    if (extent.Y > extent.X) axis = 1;
    if (extent.Z > (axis == 0 ? extent.X : extent.Y)) axis = 2;

    // Sort triangle indices by centroid along the chosen axis
    auto get_centroid = [&](uint32_t idx) -> float {
        AABB& b = tri_bounds[idx];
        HMM_Vec3 c = b.center();
        return (axis == 0) ? c.X : (axis == 1) ? c.Y : c.Z;
    };

    std::sort(tri_indices.begin() + start,
              tri_indices.begin() + start + count,
              [&](uint32_t a, uint32_t b) {
                  return get_centroid(a) < get_centroid(b);
              });

    // Split in the middle
    uint32_t mid = count / 2;

    node.tri_start = 0;
    node.tri_count = 0;  // internal node
    node.left  = build_recursive(triangles, tri_bounds, start, mid);
    // Re-fetch node reference since vector may have reallocated
    nodes[node_idx].right = build_recursive(triangles, tri_bounds, start + mid, count - mid);
    nodes[node_idx].left  = node.left;

    return node_idx;
}

// ============================================================
//  BVH Raycast
// ============================================================

HitResult BVH::raycast(const std::vector<Triangle>& triangles,
                       HMM_Vec3 origin, HMM_Vec3 dir, float max_dist) const
{
    HitResult result;
    if (nodes.empty()) return result;

    // Precompute inverse direction for slab test
    HMM_Vec3 inv_dir = HMM_V3(
        (fabsf(dir.X) > 1e-8f) ? 1.0f / dir.X : 1e30f,
        (fabsf(dir.Y) > 1e-8f) ? 1.0f / dir.Y : 1e30f,
        (fabsf(dir.Z) > 1e-8f) ? 1.0f / dir.Z : 1e30f
    );

    // Stack-based traversal (no recursion)
    uint32_t stack[64];
    int sp = 0;
    stack[sp++] = 0;  // root

    while (sp > 0) {
        uint32_t idx = stack[--sp];
        const BVHNode& node = nodes[idx];

        if (!node.bounds.intersects_ray(origin, inv_dir, result.hit ? result.t : max_dist))
            continue;

        if (node.tri_count > 0) {
            // Leaf — test triangles
            for (uint32_t i = node.tri_start; i < node.tri_start + node.tri_count; i++) {
                const Triangle& tri = triangles[tri_indices[i]];
                float t = ray_triangle(origin, dir, tri.v0, tri.v1, tri.v2);
                if (t >= 0.0f && t < max_dist && t < result.t) {
                    result.hit    = true;
                    result.t      = t;
                    result.point  = HMM_AddV3(origin, HMM_MulV3F(dir, t));
                    result.normal = tri.normal;
                }
            }
        } else {
            // Internal — push children
            stack[sp++] = node.left;
            stack[sp++] = nodes[idx].right;
        }
    }

    return result;
}

// ============================================================
//  BVH Sphere Overlap
// ============================================================

bool BVH::sphere_overlap(const std::vector<Triangle>& triangles,
                          HMM_Vec3 center, float radius,
                          HMM_Vec3& push_out, float& penetration) const
{
    bool any_hit = false;
    penetration = 0.0f;

    if (nodes.empty()) return false;

    uint32_t stack[64];
    int sp = 0;
    stack[sp++] = 0;

    while (sp > 0) {
        uint32_t idx = stack[--sp];
        const BVHNode& node = nodes[idx];

        if (!node.bounds.intersects_sphere(center, radius))
            continue;

        if (node.tri_count > 0) {
            // Leaf
            for (uint32_t i = node.tri_start; i < node.tri_start + node.tri_count; i++) {
                const Triangle& tri = triangles[tri_indices[i]];
                HMM_Vec3 closest = closest_point_on_triangle(center, tri.v0, tri.v1, tri.v2);
                HMM_Vec3 delta = HMM_SubV3(center, closest);
                float dist_sq = HMM_DotV3(delta, delta);

                if (dist_sq < radius * radius && dist_sq > 1e-12f) {
                    float dist = sqrtf(dist_sq);
                    float pen  = radius - dist;
                    if (pen > penetration) {
                        penetration = pen;
                        push_out = HMM_MulV3F(delta, 1.0f / dist);
                        any_hit = true;
                    }
                }
            }
        } else {
            stack[sp++] = node.left;
            stack[sp++] = nodes[idx].right;
        }
    }

    return any_hit;
}
