#pragma once

#include "vendor/HandmadeMath.h"
#include "geo_types.h"
#include <vector>
#include <cstdint>
#include <cmath>

// ============================================================
//  Axis-Aligned Bounding Box
// ============================================================

struct AABB {
    HMM_Vec3 min;
    HMM_Vec3 max;

    void expand(HMM_Vec3 p) {
        if (p.X < min.X) min.X = p.X;
        if (p.Y < min.Y) min.Y = p.Y;
        if (p.Z < min.Z) min.Z = p.Z;
        if (p.X > max.X) max.X = p.X;
        if (p.Y > max.Y) max.Y = p.Y;
        if (p.Z > max.Z) max.Z = p.Z;
    }

    void expand(const AABB& other) {
        expand(other.min);
        expand(other.max);
    }

    // Inflate the box by a margin on all sides
    AABB inflated(float margin) const {
        return {
            HMM_V3(min.X - margin, min.Y - margin, min.Z - margin),
            HMM_V3(max.X + margin, max.Y + margin, max.Z + margin)
        };
    }

    HMM_Vec3 center() const {
        return HMM_MulV3F(HMM_AddV3(min, max), 0.5f);
    }

    // Check if a sphere intersects this AABB
    bool intersects_sphere(HMM_Vec3 c, float r) const {
        // Closest point on AABB to sphere center
        float cx = (c.X < min.X) ? min.X : (c.X > max.X) ? max.X : c.X;
        float cy = (c.Y < min.Y) ? min.Y : (c.Y > max.Y) ? max.Y : c.Y;
        float cz = (c.Z < min.Z) ? min.Z : (c.Z > max.Z) ? max.Z : c.Z;
        float dx = c.X - cx, dy = c.Y - cy, dz = c.Z - cz;
        return (dx*dx + dy*dy + dz*dz) <= r * r;
    }

    // Check if a ray intersects this AABB (slab method)
    bool intersects_ray(HMM_Vec3 origin, HMM_Vec3 inv_dir, float max_t) const {
        float t1 = (min.X - origin.X) * inv_dir.X;
        float t2 = (max.X - origin.X) * inv_dir.X;
        float t3 = (min.Y - origin.Y) * inv_dir.Y;
        float t4 = (max.Y - origin.Y) * inv_dir.Y;
        float t5 = (min.Z - origin.Z) * inv_dir.Z;
        float t6 = (max.Z - origin.Z) * inv_dir.Z;

        float tmin = fmaxf(fmaxf(fminf(t1, t2), fminf(t3, t4)), fminf(t5, t6));
        float tmax = fminf(fminf(fmaxf(t1, t2), fmaxf(t3, t4)), fmaxf(t5, t6));

        return tmax >= 0 && tmin <= tmax && tmin <= max_t;
    }

    static AABB empty() {
        return { HMM_V3(1e30f, 1e30f, 1e30f), HMM_V3(-1e30f, -1e30f, -1e30f) };
    }

    static AABB from_triangle(const Triangle& tri);
};

// ============================================================
//  BVH Node (binary tree, stored flat in an array)
// ============================================================

struct BVHNode {
    AABB bounds;
    uint32_t left;       // index of left child (0 = leaf)
    uint32_t right;      // index of right child (0 = leaf)
    uint32_t tri_start;  // first triangle index (if leaf)
    uint32_t tri_count;  // number of triangles (0 = internal node)
};

// ============================================================
//  BVH — accelerates raycast and sphere overlap queries
// ============================================================

struct BVH {
    std::vector<BVHNode> nodes;
    std::vector<uint32_t> tri_indices;  // reordered triangle indices

    // Build from a CollisionWorld's triangles
    void build(const std::vector<Triangle>& triangles);

    // Raycast using BVH acceleration
    HitResult raycast(const std::vector<Triangle>& triangles,
                      HMM_Vec3 origin, HMM_Vec3 dir, float max_dist) const;

    // Sphere overlap using BVH acceleration
    bool sphere_overlap(const std::vector<Triangle>& triangles,
                        HMM_Vec3 center, float radius,
                        HMM_Vec3& push_out, float& penetration) const;

private:
    uint32_t build_recursive(const std::vector<Triangle>& triangles,
                             std::vector<AABB>& tri_bounds,
                             uint32_t start, uint32_t count);
};
