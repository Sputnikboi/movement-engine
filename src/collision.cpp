#include "collision.h"
#include <cmath>
#include <cstdio>

bool g_collision_log = false;

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
//  Sphere overlap test against all triangles
//  Returns the DEEPEST single penetration
// ============================================================

bool CollisionWorld::sphere_overlap(HMM_Vec3 center, float radius,
                                    HMM_Vec3& push_out, float& penetration) const
{
    bool any_hit = false;
    penetration = 0.0f;
    int hit_count = 0;

    for (size_t idx = 0; idx < triangles.size(); idx++) {
        auto& tri = triangles[idx];
        HMM_Vec3 closest = closest_point_on_triangle(center, tri.v0, tri.v1, tri.v2);
        HMM_Vec3 delta = HMM_SubV3(center, closest);
        float dist_sq = HMM_DotV3(delta, delta);

        if (dist_sq < radius * radius && dist_sq > 1e-12f) {
            float dist = sqrtf(dist_sq);
            float pen  = radius - dist;
            hit_count++;

            if (g_collision_log) {
                HMM_Vec3 push_dir = HMM_MulV3F(delta, 1.0f / dist);
                printf("  overlap tri %zu: pen=%.4f push=(%.2f,%.2f,%.2f) tri_n=(%.2f,%.2f,%.2f) closest=(%.2f,%.2f,%.2f)\n",
                       idx, pen,
                       push_dir.X, push_dir.Y, push_dir.Z,
                       tri.normal.X, tri.normal.Y, tri.normal.Z,
                       closest.X, closest.Y, closest.Z);
            }

            if (pen > penetration) {
                penetration = pen;
                push_out = HMM_MulV3F(delta, 1.0f / dist);
                any_hit = true;
            }
        }
    }

    if (g_collision_log && any_hit) {
        printf("  sphere_overlap: center=(%.2f,%.2f,%.2f) r=%.2f hits=%d winner_pen=%.4f winner_push=(%.2f,%.2f,%.2f)\n",
               center.X, center.Y, center.Z, radius, hit_count,
               penetration, push_out.X, push_out.Y, push_out.Z);
    }

    return any_hit;
}

// ============================================================
//  Quake-style ClipVelocity
//  Removes the component of velocity going into the plane,
//  with a tiny overbounce to prevent floating-point sticking.
// ============================================================

static HMM_Vec3 clip_velocity(HMM_Vec3 vel, HMM_Vec3 normal, float overbounce = 1.001f) {
    float backoff = HMM_DotV3(vel, normal) * overbounce;
    HMM_Vec3 result = HMM_SubV3(vel, HMM_MulV3F(normal, backoff));

    // Zero out tiny components to prevent drift
    if (fabsf(result.X) < 1e-5f) result.X = 0.0f;
    if (fabsf(result.Y) < 1e-5f) result.Y = 0.0f;
    if (fabsf(result.Z) < 1e-5f) result.Z = 0.0f;

    return result;
}

// ============================================================
//  Slide move: move sphere through world, sliding along surfaces
//  This is the Quake PM_SlideMove algorithm.
//  - Try to move full displacement
//  - If overlap, push out and clip velocity against contact plane
//  - Repeat up to 4 times (handles corners/wedges)
// ============================================================

HMM_Vec3 CollisionWorld::slide_move(HMM_Vec3 start, float radius,
                                     HMM_Vec3& velocity, float dt) const
{
    constexpr int MAX_CLIPS = 4;

    HMM_Vec3 pos = start;
    HMM_Vec3 remaining = HMM_MulV3F(velocity, dt);
    HMM_Vec3 planes[MAX_CLIPS];
    int num_planes = 0;

    if (g_collision_log) {
        printf("slide_move START pos=(%.2f,%.2f,%.2f) vel=(%.2f,%.2f,%.2f) dt=%.5f\n",
               pos.X, pos.Y, pos.Z, velocity.X, velocity.Y, velocity.Z, dt);
    }

    for (int i = 0; i < MAX_CLIPS; i++) {
        // Move by remaining displacement
        HMM_Vec3 prev_pos = pos;
        pos = HMM_AddV3(pos, remaining);

        if (g_collision_log) {
            printf(" clip %d: moved (%.2f,%.2f,%.2f) -> (%.2f,%.2f,%.2f)\n",
                   i, prev_pos.X, prev_pos.Y, prev_pos.Z, pos.X, pos.Y, pos.Z);
        }

        // Check for overlap
        HMM_Vec3 push_dir;
        float pen;
        if (!sphere_overlap(pos, radius, push_dir, pen)) {
            if (g_collision_log) printf(" clip %d: no overlap, done\n", i);
            break;  // no collision, done
        }

        // Push out of geometry
        HMM_Vec3 pre_push = pos;
        pos = HMM_AddV3(pos, HMM_MulV3F(push_dir, pen + 0.001f));

        if (g_collision_log) {
            printf(" clip %d: OVERLAP pen=%.4f push_dir=(%.2f,%.2f,%.2f) pushed (%.2f,%.2f,%.2f) -> (%.2f,%.2f,%.2f)\n",
                   i, pen, push_dir.X, push_dir.Y, push_dir.Z,
                   pre_push.X, pre_push.Y, pre_push.Z,
                   pos.X, pos.Y, pos.Z);
        }

        // Record this plane
        if (num_planes < MAX_CLIPS)
            planes[num_planes++] = push_dir;

        // Clip velocity against this plane
        HMM_Vec3 vel_before = velocity;
        velocity = clip_velocity(velocity, push_dir);

        if (g_collision_log) {
            printf(" clip %d: vel (%.2f,%.2f,%.2f) -> (%.2f,%.2f,%.2f)\n",
                   i, vel_before.X, vel_before.Y, vel_before.Z,
                   velocity.X, velocity.Y, velocity.Z);
        }

        remaining = HMM_MulV3F(push_dir, 0.0f);  // stop remaining movement this iteration

        // Check if we're being pushed into a corner (velocity opposing all planes)
        bool stuck = false;
        for (int j = 0; j < num_planes; j++) {
            if (HMM_DotV3(velocity, planes[j]) < -0.01f) {
                velocity = clip_velocity(velocity, planes[j]);
                stuck = true;
            }
        }
        if (stuck) {
            bool still_stuck = false;
            for (int j = 0; j < num_planes; j++) {
                if (HMM_DotV3(velocity, planes[j]) < -0.01f) {
                    still_stuck = true;
                    break;
                }
            }
            if (still_stuck) {
                if (g_collision_log) printf(" clip %d: STUCK in corner, zeroing velocity\n", i);
                velocity = HMM_V3(0, 0, 0);
                break;
            }
        }
    }

    if (g_collision_log) {
        printf("slide_move END pos=(%.2f,%.2f,%.2f) vel=(%.2f,%.2f,%.2f)\n\n",
               pos.X, pos.Y, pos.Z, velocity.X, velocity.Y, velocity.Z);
    }

    return pos;
}
