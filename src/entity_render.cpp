#include "entity_render.h"
#include <cmath>
#include <map>

// ============================================================
//  Icosphere generation
// ============================================================

using EdgeKey = uint64_t;
static EdgeKey make_edge_key(uint32_t a, uint32_t b) {
    if (a > b) { uint32_t t = a; a = b; b = t; }
    return (static_cast<uint64_t>(a) << 32) | b;
}

static uint32_t get_midpoint(std::vector<HMM_Vec3>& verts,
                             std::map<EdgeKey, uint32_t>& cache,
                             uint32_t a, uint32_t b)
{
    EdgeKey key = make_edge_key(a, b);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;

    HMM_Vec3 mid = HMM_MulV3F(HMM_AddV3(verts[a], verts[b]), 0.5f);
    // Project onto unit sphere
    float len = HMM_LenV3(mid);
    if (len > 0.0001f) mid = HMM_MulV3F(mid, 1.0f / len);

    uint32_t idx = static_cast<uint32_t>(verts.size());
    verts.push_back(mid);
    cache[key] = idx;
    return idx;
}

Mesh create_icosphere(int subdivisions) {
    // Start with icosahedron
    const float t = (1.0f + sqrtf(5.0f)) / 2.0f;
    auto norm = [](float x, float y, float z) -> HMM_Vec3 {
        float len = sqrtf(x*x + y*y + z*z);
        return HMM_V3(x/len, y/len, z/len);
    };

    std::vector<HMM_Vec3> verts = {
        norm(-1,  t, 0), norm( 1,  t, 0), norm(-1, -t, 0), norm( 1, -t, 0),
        norm( 0, -1,  t), norm( 0,  1,  t), norm( 0, -1, -t), norm( 0,  1, -t),
        norm( t, 0, -1), norm( t, 0,  1), norm(-t, 0, -1), norm(-t, 0,  1),
    };

    std::vector<uint32_t> tris = {
        0,11,5,  0,5,1,  0,1,7,  0,7,10, 0,10,11,
        1,5,9,   5,11,4, 11,10,2, 10,7,6, 7,1,8,
        3,9,4,   3,4,2,  3,2,6,  3,6,8,  3,8,9,
        4,9,5,   2,4,11, 6,2,10, 8,6,7,  9,8,1,
    };

    // Subdivide
    for (int s = 0; s < subdivisions; s++) {
        std::map<EdgeKey, uint32_t> cache;
        std::vector<uint32_t> new_tris;
        new_tris.reserve(tris.size() * 4);

        for (size_t i = 0; i < tris.size(); i += 3) {
            uint32_t a = tris[i], b = tris[i+1], c = tris[i+2];
            uint32_t ab = get_midpoint(verts, cache, a, b);
            uint32_t bc = get_midpoint(verts, cache, b, c);
            uint32_t ca = get_midpoint(verts, cache, c, a);

            new_tris.insert(new_tris.end(), {a, ab, ca});
            new_tris.insert(new_tris.end(), {b, bc, ab});
            new_tris.insert(new_tris.end(), {c, ca, bc});
            new_tris.insert(new_tris.end(), {ab, bc, ca});
        }
        tris = std::move(new_tris);
    }

    // Build Mesh with normals = positions (unit sphere)
    Mesh mesh;
    mesh.vertices.resize(verts.size());
    for (size_t i = 0; i < verts.size(); i++) {
        mesh.vertices[i].pos[0] = verts[i].X;
        mesh.vertices[i].pos[1] = verts[i].Y;
        mesh.vertices[i].pos[2] = verts[i].Z;
        mesh.vertices[i].normal[0] = verts[i].X;
        mesh.vertices[i].normal[1] = verts[i].Y;
        mesh.vertices[i].normal[2] = verts[i].Z;
        mesh.vertices[i].color[0] = 1.0f;
        mesh.vertices[i].color[1] = 1.0f;
        mesh.vertices[i].color[2] = 1.0f;
    }
    mesh.indices = tris;
    return mesh;
}

// ============================================================
//  Build entity mesh for rendering
// ============================================================

// Append a scaled/translated copy of the base sphere into out
static void append_sphere(Mesh& out, const Mesh& sphere,
                          HMM_Vec3 pos, float scale,
                          float r, float g, float b)
{
    uint32_t base = static_cast<uint32_t>(out.vertices.size());

    for (auto& sv : sphere.vertices) {
        Vertex3D v;
        v.pos[0] = sv.pos[0] * scale + pos.X;
        v.pos[1] = sv.pos[1] * scale + pos.Y;
        v.pos[2] = sv.pos[2] * scale + pos.Z;
        v.normal[0] = sv.normal[0];
        v.normal[1] = sv.normal[1];
        v.normal[2] = sv.normal[2];
        v.color[0] = r;
        v.color[1] = g;
        v.color[2] = b;
        out.vertices.push_back(v);
    }

    for (auto idx : sphere.indices)
        out.indices.push_back(base + idx);
}

Mesh build_entity_mesh(const Entity entities[], int max_entities) {
    // Create sphere once (static)
    static Mesh sphere = create_icosphere(2);

    Mesh out;

    for (int i = 0; i < max_entities; i++) {
        const Entity& e = entities[i];
        if (!e.alive) continue;

        switch (e.type) {
        case EntityType::Drone: {
            // Health-based color: green -> red as health drops
            float hp_frac = (e.max_health > 0) ? e.health / e.max_health : 0.0f;
            float r = 1.0f - hp_frac;
            float g = hp_frac * 0.5f + 0.2f;
            float b = 0.3f;
            append_sphere(out, sphere, e.position, e.radius, r, g, b);
        } break;

        case EntityType::Projectile: {
            // Bright orange-yellow
            append_sphere(out, sphere, e.position, e.radius, 1.0f, 0.7f, 0.1f);
        } break;

        default: break;
        }
    }

    return out;
}
