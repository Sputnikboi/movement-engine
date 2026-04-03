#include "collision.h"
#include <cmath>
#include <cstdio>

// ============================================================
//  Build triangle list from Mesh
// ============================================================

void CollisionWorld::build_from_mesh(const Mesh& mesh) {
    triangles.clear();
    triangles.reserve(mesh.indices.size() / 3);

    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        Triangle tri;
        auto load = [](const float* p) { return HMM_V3(p[0], p[1], p[2]); };

        tri.v0 = load(mesh.vertices[mesh.indices[i + 0]].pos);
        tri.v1 = load(mesh.vertices[mesh.indices[i + 1]].pos);
        tri.v2 = load(mesh.vertices[mesh.indices[i + 2]].pos);

        HMM_Vec3 e1 = HMM_SubV3(tri.v1, tri.v0);
        HMM_Vec3 e2 = HMM_SubV3(tri.v2, tri.v0);
        tri.normal = HMM_NormV3(HMM_Cross(e1, e2));

        // Skip degenerate triangles
        float len = HMM_LenV3(HMM_Cross(e1, e2));
        if (len > 0.0001f)
            triangles.push_back(tri);
    }

    fprintf(stdout, "CollisionWorld: %zu triangles\n", triangles.size());
}

// ============================================================
//  Möller–Trumbore ray-triangle intersection
// ============================================================

float ray_triangle(HMM_Vec3 origin, HMM_Vec3 dir,
                   HMM_Vec3 v0, HMM_Vec3 v1, HMM_Vec3 v2)
{
    constexpr float EPSILON = 1e-7f;

    HMM_Vec3 e1 = HMM_SubV3(v1, v0);
    HMM_Vec3 e2 = HMM_SubV3(v2, v0);
    HMM_Vec3 h  = HMM_Cross(dir, e2);
    float a = HMM_DotV3(e1, h);

    if (a > -EPSILON && a < EPSILON)
        return -1.0f;  // parallel

    float f = 1.0f / a;
    HMM_Vec3 s = HMM_SubV3(origin, v0);
    float u = f * HMM_DotV3(s, h);
    if (u < 0.0f || u > 1.0f)
        return -1.0f;

    HMM_Vec3 q = HMM_Cross(s, e1);
    float v = f * HMM_DotV3(dir, q);
    if (v < 0.0f || u + v > 1.0f)
        return -1.0f;

    float t = f * HMM_DotV3(e2, q);
    if (t > EPSILON)
        return t;

    return -1.0f;
}

// ============================================================
//  Closest point on triangle to a point
//  (Christer Ericson's "Real-Time Collision Detection" method)
// ============================================================

HMM_Vec3 closest_point_on_triangle(HMM_Vec3 p, HMM_Vec3 a, HMM_Vec3 b, HMM_Vec3 c) {
    HMM_Vec3 ab = HMM_SubV3(b, a);
    HMM_Vec3 ac = HMM_SubV3(c, a);
    HMM_Vec3 ap = HMM_SubV3(p, a);

    float d1 = HMM_DotV3(ab, ap);
    float d2 = HMM_DotV3(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return a;  // vertex A

    HMM_Vec3 bp = HMM_SubV3(p, b);
    float d3 = HMM_DotV3(ab, bp);
    float d4 = HMM_DotV3(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) return b;  // vertex B

    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        float v = d1 / (d1 - d3);
        return HMM_AddV3(a, HMM_MulV3F(ab, v));  // edge AB
    }

    HMM_Vec3 cp = HMM_SubV3(p, c);
    float d5 = HMM_DotV3(ab, cp);
    float d6 = HMM_DotV3(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) return c;  // vertex C

    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        float w = d2 / (d2 - d6);
        return HMM_AddV3(a, HMM_MulV3F(ac, w));  // edge AC
    }

    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return HMM_AddV3(b, HMM_MulV3F(HMM_SubV3(c, b), w));  // edge BC
    }

    // Inside triangle
    float denom = 1.0f / (va + vb + vc);
    float v = vb * denom;
    float w = vc * denom;
    return HMM_AddV3(a, HMM_AddV3(HMM_MulV3F(ab, v), HMM_MulV3F(ac, w)));
}

// ============================================================
//  Raycast against all triangles
// ============================================================

HitResult CollisionWorld::raycast(HMM_Vec3 origin, HMM_Vec3 dir, float max_dist) const {
    HitResult result;

    for (auto& tri : triangles) {
        float t = ray_triangle(origin, dir, tri.v0, tri.v1, tri.v2);
        if (t >= 0.0f && t < max_dist && t < result.t) {
            result.hit    = true;
            result.t      = t;
            result.point  = HMM_AddV3(origin, HMM_MulV3F(dir, t));
            result.normal = tri.normal;
        }
    }

    return result;
}

// ============================================================
//  Iterative depenetration
//  Resolves ALL sphere-vs-triangle overlaps by repeatedly
//  finding and resolving the deepest one. Handles corners
//  where multiple faces overlap simultaneously.
// ============================================================

bool CollisionWorld::depenetrate(HMM_Vec3& center, float radius) const {
    constexpr int MAX_ITERS = 8;
    bool any_resolved = false;

    for (int iter = 0; iter < MAX_ITERS; iter++) {
        // Find deepest overlapping triangle
        float worst_pen = 0.0f;
        HMM_Vec3 worst_push = {};
        bool found = false;

        for (auto& tri : triangles) {
            HMM_Vec3 closest = closest_point_on_triangle(center, tri.v0, tri.v1, tri.v2);
            HMM_Vec3 delta = HMM_SubV3(center, closest);
            float dist_sq = HMM_DotV3(delta, delta);

            if (dist_sq < radius * radius) {
                if (dist_sq < 1e-12f) {
                    // Sphere center is exactly on the triangle — use face normal
                    float pen = radius;
                    if (pen > worst_pen) {
                        worst_pen = pen;
                        worst_push = tri.normal;
                        found = true;
                    }
                } else {
                    float dist = sqrtf(dist_sq);
                    float pen = radius - dist;
                    if (pen > worst_pen) {
                        worst_pen = pen;
                        worst_push = HMM_MulV3F(delta, 1.0f / dist);
                        found = true;
                    }
                }
            }
        }

        if (!found) break;  // fully resolved

        // Push out with small epsilon to prevent re-overlap
        center = HMM_AddV3(center, HMM_MulV3F(worst_push, worst_pen + 0.002f));
        any_resolved = true;
    }

    return any_resolved;
}

// ============================================================
//  Quake-style ClipVelocity
//  Removes the component of velocity going into the plane.
// ============================================================

static HMM_Vec3 clip_velocity(HMM_Vec3 vel, HMM_Vec3 normal, float overbounce = 1.001f) {
    float backoff = HMM_DotV3(vel, normal) * overbounce;
    return HMM_SubV3(vel, HMM_MulV3F(normal, backoff));
}

// ============================================================
//  Slide move: Quake PM_SlideMove
//  Moves the sphere incrementally, clipping velocity against
//  each contact plane. Properly continues sliding along walls
//  instead of stopping dead on first contact.
// ============================================================

HMM_Vec3 CollisionWorld::slide_move(HMM_Vec3 start, float radius,
                                     HMM_Vec3& velocity, float dt) const
{
    constexpr int MAX_BUMPS = 4;
    constexpr float MIN_MOVE = 0.001f;

    HMM_Vec3 pos = start;
    float time_left = dt;
    HMM_Vec3 planes[MAX_BUMPS];
    int num_planes = 0;

    // Pre-clip: if starting overlapping, depenetrate first
    depenetrate(pos, radius);

    HMM_Vec3 original_vel = velocity;

    for (int bump = 0; bump < MAX_BUMPS; bump++) {
        if (time_left <= 0.0f) break;

        HMM_Vec3 displacement = HMM_MulV3F(velocity, time_left);
        float disp_len = HMM_LenV3(displacement);
        if (disp_len < MIN_MOVE) break;

        // Try the full move
        HMM_Vec3 target = HMM_AddV3(pos, displacement);

        // Binary search for how far we can go before overlapping.
        // This finds a safe fraction of the displacement.
        float lo = 0.0f, hi = 1.0f;
        HMM_Vec3 safe_pos = pos;
        bool hit_something = false;

        // Quick check: is the target clear?
        HMM_Vec3 test = target;
        // Check overlap at target
        HMM_Vec3 test_copy = test;
        bool overlaps = false;
        for (auto& tri : triangles) {
            HMM_Vec3 closest = closest_point_on_triangle(test_copy, tri.v0, tri.v1, tri.v2);
            HMM_Vec3 delta = HMM_SubV3(test_copy, closest);
            if (HMM_DotV3(delta, delta) < radius * radius) {
                overlaps = true;
                break;
            }
        }

        if (!overlaps) {
            // No collision — move the full distance
            pos = target;
            break;
        }

        // Binary search for contact point (6 iterations = 1.5% precision)
        hit_something = true;
        for (int s = 0; s < 6; s++) {
            float mid = (lo + hi) * 0.5f;
            HMM_Vec3 mid_pos = HMM_AddV3(pos, HMM_MulV3F(displacement, mid));

            bool mid_overlaps = false;
            for (auto& tri : triangles) {
                HMM_Vec3 closest = closest_point_on_triangle(mid_pos, tri.v0, tri.v1, tri.v2);
                HMM_Vec3 delta = HMM_SubV3(mid_pos, closest);
                if (HMM_DotV3(delta, delta) < radius * radius) {
                    mid_overlaps = true;
                    break;
                }
            }

            if (mid_overlaps) {
                hi = mid;
            } else {
                lo = mid;
                safe_pos = mid_pos;
            }
        }

        // Move to safe position
        pos = safe_pos;
        float fraction_used = lo;
        time_left *= (1.0f - fraction_used);

        // Depenetrate at contact point (in case binary search wasn't exact)
        depenetrate(pos, radius);

        // Find the contact normal — deepest overlapping triangle at the target
        HMM_Vec3 contact_normal = {};
        {
            float worst_pen = 0.0f;
            HMM_Vec3 probe = HMM_AddV3(pos, HMM_MulV3F(displacement, 0.01f));
            for (auto& tri : triangles) {
                HMM_Vec3 closest = closest_point_on_triangle(probe, tri.v0, tri.v1, tri.v2);
                HMM_Vec3 delta = HMM_SubV3(probe, closest);
                float dist_sq = HMM_DotV3(delta, delta);
                if (dist_sq < radius * radius) {
                    float dist = (dist_sq > 1e-12f) ? sqrtf(dist_sq) : 0.0f;
                    float pen = radius - dist;
                    if (pen > worst_pen) {
                        worst_pen = pen;
                        if (dist > 1e-6f)
                            contact_normal = HMM_MulV3F(delta, 1.0f / dist);
                        else
                            contact_normal = tri.normal;
                    }
                }
            }
        }

        float cn_len = HMM_LenV3(contact_normal);
        if (cn_len < 0.5f) break;  // couldn't determine normal
        contact_normal = HMM_MulV3F(contact_normal, 1.0f / cn_len);

        // Record plane
        if (num_planes < MAX_BUMPS)
            planes[num_planes++] = contact_normal;

        // Clip velocity against this plane
        velocity = clip_velocity(velocity, contact_normal);

        // Also clip against all previously accumulated planes
        bool dead = false;
        for (int j = 0; j < num_planes - 1; j++) {
            if (HMM_DotV3(velocity, planes[j]) < 0.0f) {
                velocity = clip_velocity(velocity, planes[j]);

                // If now going into the latest plane, we're in a crease
                if (HMM_DotV3(velocity, contact_normal) < 0.0f) {
                    // Slide along the crease (cross product of the two planes)
                    HMM_Vec3 crease = HMM_Cross(contact_normal, planes[j]);
                    float crease_len = HMM_LenV3(crease);
                    if (crease_len > 0.001f) {
                        crease = HMM_MulV3F(crease, 1.0f / crease_len);
                        float d = HMM_DotV3(velocity, crease);
                        velocity = HMM_MulV3F(crease, d);
                    } else {
                        velocity = HMM_V3(0, 0, 0);
                        dead = true;
                    }
                }
            }
        }

        if (dead || HMM_LenV3(velocity) < MIN_MOVE) {
            velocity = HMM_V3(0, 0, 0);
            break;
        }
    }

    return pos;
}
