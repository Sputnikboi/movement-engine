#include "mesh.h"
#include <cmath>

// ============================================================
//  Helpers
// ============================================================

static void normalize(float out[3], float x, float y, float z) {
    float len = sqrtf(x*x + y*y + z*z);
    if (len > 0.0001f) { out[0] = x/len; out[1] = y/len; out[2] = z/len; }
    else               { out[0] = 0; out[1] = 1; out[2] = 0; }
}

static void cross(float out[3], const float a[3], const float b[3]) {
    out[0] = a[1]*b[2] - a[2]*b[1];
    out[1] = a[2]*b[0] - a[0]*b[2];
    out[2] = a[0]*b[1] - a[1]*b[0];
}

// Add a quad (two triangles) with an auto-computed normal.
// Winding order: p0 → p1 → p2 → p3 (counter-clockwise when viewed from front)
static void add_quad(Mesh& mesh,
                     float p0[3], float p1[3], float p2[3], float p3[3],
                     float color[3])
{
    // Compute face normal from edges p0→p1 and p0→p3
    float e1[3] = { p1[0]-p0[0], p1[1]-p0[1], p1[2]-p0[2] };
    float e2[3] = { p3[0]-p0[0], p3[1]-p0[1], p3[2]-p0[2] };
    float n[3];
    cross(n, e1, e2);
    normalize(n, n[0], n[1], n[2]);

    uint32_t base = static_cast<uint32_t>(mesh.vertices.size());

    auto push_vert = [&](float p[3]) {
        Vertex3D v;
        v.pos[0] = p[0]; v.pos[1] = p[1]; v.pos[2] = p[2];
        v.normal[0] = n[0]; v.normal[1] = n[1]; v.normal[2] = n[2];
        v.color[0] = color[0]; v.color[1] = color[1]; v.color[2] = color[2];
        mesh.vertices.push_back(v);
    };

    push_vert(p0);
    push_vert(p1);
    push_vert(p2);
    push_vert(p3);

    // Triangle 1: p0, p1, p2
    mesh.indices.push_back(base + 0);
    mesh.indices.push_back(base + 1);
    mesh.indices.push_back(base + 2);
    // Triangle 2: p0, p2, p3
    mesh.indices.push_back(base + 0);
    mesh.indices.push_back(base + 2);
    mesh.indices.push_back(base + 3);
}

// Add an axis-aligned box as 6 quads. min = bottom-left-back, max = top-right-front.
static void add_box(Mesh& mesh,
                    float min_x, float min_y, float min_z,
                    float max_x, float max_y, float max_z,
                    float color[3])
{
    // 8 corners
    float v000[3] = {min_x, min_y, min_z};
    float v100[3] = {max_x, min_y, min_z};
    float v110[3] = {max_x, max_y, min_z};
    float v010[3] = {min_x, max_y, min_z};
    float v001[3] = {min_x, min_y, max_z};
    float v101[3] = {max_x, min_y, max_z};
    float v111[3] = {max_x, max_y, max_z};
    float v011[3] = {min_x, max_y, max_z};

    // Front face (+Z)
    add_quad(mesh, v001, v101, v111, v011, color);
    // Back face (-Z)
    add_quad(mesh, v100, v000, v010, v110, color);
    // Right face (+X)
    add_quad(mesh, v101, v100, v110, v111, color);
    // Left face (-X)
    add_quad(mesh, v000, v001, v011, v010, color);
    // Top face (+Y)
    add_quad(mesh, v010, v011, v111, v110, color);
    // Bottom face (-Y)
    add_quad(mesh, v001, v000, v100, v101, color);
}

// ============================================================
//  Test level
// ============================================================

Mesh create_test_level() {
    Mesh mesh;

    // --- Colors ---
    float floor_dark[3]  = {0.20f, 0.20f, 0.22f};
    float floor_light[3] = {0.28f, 0.28f, 0.30f};
    float wall_color[3]  = {0.35f, 0.35f, 0.38f};
    float ramp_color[3]  = {0.70f, 0.45f, 0.20f};
    float plat_color[3]  = {0.25f, 0.40f, 0.65f};
    float box_color[3]   = {0.55f, 0.25f, 0.25f};
    float trim_color[3]  = {0.80f, 0.75f, 0.20f};

    // --- Checkerboard floor (50x50, centered at origin) ---
    // 10x10 grid of 5x5 tiles
    float half = 25.0f;
    float tile = 5.0f;
    for (int gx = 0; gx < 10; gx++) {
        for (int gz = 0; gz < 10; gz++) {
            float x0 = -half + gx * tile;
            float z0 = -half + gz * tile;
            float x1 = x0 + tile;
            float z1 = z0 + tile;
            float* col = ((gx + gz) % 2 == 0) ? floor_dark : floor_light;

            float p0[3] = {x0, 0, z1};
            float p1[3] = {x1, 0, z1};
            float p2[3] = {x1, 0, z0};
            float p3[3] = {x0, 0, z0};
            add_quad(mesh, p0, p1, p2, p3, col);
        }
    }

    // --- Perimeter walls (height = 8) ---
    float wh = 8.0f;
    float wt = 0.5f;  // wall thickness

    // South wall (-Z side)
    add_box(mesh, -half, 0, -half - wt, half, wh, -half, wall_color);
    // North wall (+Z side)
    add_box(mesh, -half, 0, half, half, wh, half + wt, wall_color);
    // West wall (-X side)
    add_box(mesh, -half - wt, 0, -half, -half, wh, half, wall_color);
    // East wall (+X side)
    add_box(mesh, half, 0, -half, half + wt, wh, half, wall_color);

    // --- Ramp (southeast corner, rises from y=0 to y=5) ---
    {
        float rx = 8.0f, rz = -15.0f;   // base position
        float rw = 8.0f, rl = 12.0f;    // width, length
        float rh = 5.0f;                 // height at top

        // Ramp surface: bottom edge at (rx, 0, rz+rl), top edge at (rx, rh, rz)
        float p0[3] = {rx,      0.0f, rz + rl};
        float p1[3] = {rx + rw, 0.0f, rz + rl};
        float p2[3] = {rx + rw, rh,   rz};
        float p3[3] = {rx,      rh,   rz};
        add_quad(mesh, p0, p1, p2, p3, ramp_color);

        // Side walls
        float s0[3] = {rx, 0,    rz + rl};
        float s1[3] = {rx, rh,   rz};
        float s2[3] = {rx, 0,    rz};
        add_quad(mesh, s0, s1, s2, s0, ramp_color);  // degenerate, let's do triangles properly

        // Left side triangle (as a degenerate quad with p3=p0 doesn't work well,
        // so let's just use two near-degenerate verts)
        float ls0[3] = {rx, 0,  rz + rl};
        float ls1[3] = {rx, 0,  rz};
        float ls2[3] = {rx, rh, rz};
        float ls3[3] = {rx, rh, rz};  // degenerate but fine for a triangle
        add_quad(mesh, ls0, ls1, ls2, ls3, ramp_color);

        float rs0[3] = {rx + rw, 0,  rz};
        float rs1[3] = {rx + rw, 0,  rz + rl};
        float rs2[3] = {rx + rw, rh, rz};
        float rs3[3] = {rx + rw, rh, rz};
        add_quad(mesh, rs0, rs1, rs2, rs3, ramp_color);

        // Back wall of ramp
        float bw0[3] = {rx,      0,  rz};
        float bw1[3] = {rx + rw, 0,  rz};
        float bw2[3] = {rx + rw, rh, rz};
        float bw3[3] = {rx,      rh, rz};
        add_quad(mesh, bw0, bw1, bw2, bw3, ramp_color);

        // Underside
        float u0[3] = {rx + rw, 0, rz + rl};
        float u1[3] = {rx,      0, rz + rl};
        float u2[3] = {rx,      0, rz};
        float u3[3] = {rx + rw, 0, rz};
        add_quad(mesh, u0, u1, u2, u3, ramp_color);
    }

    // --- Raised platform (connected to ramp top) ---
    add_box(mesh, 6.0f, 4.5f, -22.0f, 22.0f, 5.0f, -15.0f, plat_color);

    // --- Center pillar ---
    add_box(mesh, -2.0f, 0.0f, -2.0f, 2.0f, 6.0f, 2.0f, box_color);

    // --- Some scattered obstacles ---
    // Low wall for jumping over
    add_box(mesh, -15.0f, 0.0f, 5.0f, -8.0f, 1.5f, 6.0f, trim_color);
    // Medium block
    add_box(mesh, -18.0f, 0.0f, -10.0f, -14.0f, 3.0f, -6.0f, box_color);
    // Thin tall pillar
    add_box(mesh, 18.0f, 0.0f, 15.0f, 19.5f, 7.0f, 16.5f, wall_color);
    // Step sequence (3 steps)
    add_box(mesh, -10.0f, 0.0f, 15.0f, -6.0f, 1.0f, 18.0f, plat_color);
    add_box(mesh, -6.0f,  0.0f, 15.0f, -2.0f, 2.0f, 18.0f, plat_color);
    add_box(mesh,  -2.0f, 0.0f, 15.0f,  2.0f, 3.0f, 18.0f, plat_color);

    return mesh;
}
