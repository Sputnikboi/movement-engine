#include "damage_numbers.h"
#include <cstdio>
#include <cmath>
#include <cstring>

// ============================================================
//  Spawn / Update
// ============================================================

void DamageNumberSystem::spawn(HMM_Vec3 pos, int damage, bool is_kill) {
    // Find a free slot (or oldest)
    int best = -1;
    float oldest_life = 999.0f;
    for (int i = 0; i < MAX_NUMBERS; i++) {
        if (!numbers[i].active) { best = i; break; }
        if (numbers[i].lifetime < oldest_life) {
            oldest_life = numbers[i].lifetime;
            best = i;
        }
    }
    if (best < 0) best = 0;

    DamageNumber& dn = numbers[best];
    dn.position = pos;
    dn.velocity_y = 1.8f;         // upward drift speed
    dn.max_lifetime = 0.9f;
    dn.lifetime = dn.max_lifetime;
    dn.value = damage;
    dn.is_kill = is_kill;
    dn.active = true;
}

void DamageNumberSystem::update(float dt) {
    for (int i = 0; i < MAX_NUMBERS; i++) {
        DamageNumber& dn = numbers[i];
        if (!dn.active) continue;
        dn.lifetime -= dt;
        if (dn.lifetime <= 0.0f) {
            dn.active = false;
            continue;
        }
        dn.position.Y += dn.velocity_y * dt;
        dn.velocity_y *= (1.0f - 2.0f * dt); // decelerate
    }
}

// ============================================================
//  7-segment digit geometry
// ============================================================
//
// Each digit is 7 segments arranged like:
//   _
//  |_|
//  |_|
//
// Segment indices:
//   0 = top horizontal
//   1 = top-right vertical
//   2 = bottom-right vertical
//   3 = bottom horizontal
//   4 = bottom-left vertical
//   5 = top-left vertical
//   6 = middle horizontal

// Which segments are on for each digit 0-9
static const uint8_t DIGIT_SEGMENTS[10] = {
    0b0111111, // 0: all except middle
    0b0000110, // 1: top-right, bottom-right
    0b1011011, // 2: top, top-right, middle, bottom-left, bottom
    0b1001111, // 3: top, top-right, middle, bottom-right, bottom
    0b1100110, // 4: top-left, top-right, middle, bottom-right
    0b1101101, // 5: top, top-left, middle, bottom-right, bottom
    0b1111101, // 6: top, top-left, middle, bottom-left, bottom-right, bottom
    0b0000111, // 7: top, top-right, bottom-right
    0b1111111, // 8: all
    0b1101111, // 9: all except bottom-left
};

// Add a single billboard quad (two triangles)
static void add_billboard_quad(Mesh& m,
                               HMM_Vec3 center, HMM_Vec3 right, HMM_Vec3 up,
                               float half_w, float half_h,
                               HMM_Vec3 color) {
    uint32_t base = (uint32_t)m.vertices.size();
    // Normal facing camera (approximate: cross of right × up)
    HMM_Vec3 normal = HMM_NormV3(HMM_Cross(right, up));

    auto push = [&](float rx, float uy) {
        Vertex3D v;
        HMM_Vec3 p = HMM_AddV3(center,
            HMM_AddV3(HMM_MulV3F(right, rx), HMM_MulV3F(up, uy)));
        v.pos[0] = p.X; v.pos[1] = p.Y; v.pos[2] = p.Z;
        v.normal[0] = normal.X; v.normal[1] = normal.Y; v.normal[2] = normal.Z;
        v.color[0] = color.X; v.color[1] = color.Y; v.color[2] = color.Z;
        m.vertices.push_back(v);
    };

    push(-half_w, -half_h);
    push( half_w, -half_h);
    push( half_w,  half_h);
    push(-half_w,  half_h);

    m.indices.push_back(base + 0);
    m.indices.push_back(base + 2);
    m.indices.push_back(base + 1);
    m.indices.push_back(base + 0);
    m.indices.push_back(base + 3);
    m.indices.push_back(base + 2);
}

// Draw one 7-segment digit at a given position
static void draw_digit(Mesh& out, int digit,
                       HMM_Vec3 center, HMM_Vec3 cam_right, HMM_Vec3 cam_up,
                       float char_w, float char_h, HMM_Vec3 color) {
    if (digit < 0 || digit > 9) return;
    uint8_t segs = DIGIT_SEGMENTS[digit];

    // Segment dimensions
    float seg_len = char_w * 0.8f;    // horizontal segment length
    float seg_thick = char_h * 0.08f;  // segment thickness
    float vlen = char_h * 0.38f;      // vertical segment length

    // Positions relative to digit center
    float top_y    =  char_h * 0.45f;
    float mid_y    =  0.0f;
    float bot_y    = -char_h * 0.45f;
    float left_x   = -char_w * 0.35f;
    float right_x  =  char_w * 0.35f;

    // Horizontal segments: thin wide quads
    auto h_seg = [&](float cx, float cy) {
        HMM_Vec3 pos = HMM_AddV3(center,
            HMM_AddV3(HMM_MulV3F(cam_right, cx), HMM_MulV3F(cam_up, cy)));
        add_billboard_quad(out, pos, cam_right, cam_up, seg_len * 0.5f, seg_thick, color);
    };

    // Vertical segments: tall narrow quads
    auto v_seg = [&](float cx, float cy) {
        HMM_Vec3 pos = HMM_AddV3(center,
            HMM_AddV3(HMM_MulV3F(cam_right, cx), HMM_MulV3F(cam_up, cy)));
        add_billboard_quad(out, pos, cam_right, cam_up, seg_thick, vlen * 0.5f, color);
    };

    if (segs & (1 << 0)) h_seg(0, top_y);                           // top
    if (segs & (1 << 1)) v_seg(right_x, (top_y + mid_y) * 0.5f);   // top-right
    if (segs & (1 << 2)) v_seg(right_x, (mid_y + bot_y) * 0.5f);   // bottom-right
    if (segs & (1 << 3)) h_seg(0, bot_y);                           // bottom
    if (segs & (1 << 4)) v_seg(left_x, (mid_y + bot_y) * 0.5f);    // bottom-left
    if (segs & (1 << 5)) v_seg(left_x, (top_y + mid_y) * 0.5f);    // top-left
    if (segs & (1 << 6)) h_seg(0, mid_y);                           // middle
}

// ============================================================
//  Build mesh
// ============================================================

void DamageNumberSystem::build_mesh(Mesh& out, HMM_Vec3 cam_right, HMM_Vec3 cam_up) const {
    for (int i = 0; i < MAX_NUMBERS; i++) {
        const DamageNumber& dn = numbers[i];
        if (!dn.active) continue;

        float t = 1.0f - dn.lifetime / dn.max_lifetime; // 0 → 1 over lifetime
        float alpha = (t < 0.7f) ? 1.0f : (1.0f - (t - 0.7f) / 0.3f); // fade in last 30%
        float scale = 1.0f + t * 0.3f; // grow slightly over time

        // Color: white for normal hits, red/orange for kills
        HMM_Vec3 color;
        if (dn.is_kill) {
            color = HMM_V3(1.0f * alpha, 0.3f * alpha, 0.1f * alpha);
        } else {
            color = HMM_V3(1.0f * alpha, 0.95f * alpha, 0.8f * alpha);
        }

        // Extract digits
        int val = dn.value;
        if (val < 0) val = -val;
        char digits_str[16];
        snprintf(digits_str, sizeof(digits_str), "%d", val);
        int num_digits = (int)strlen(digits_str);

        float char_w = 0.18f * scale;
        float char_h = 0.3f * scale;
        float spacing = char_w * 1.2f;
        float total_w = spacing * num_digits;

        // Center the number string on the position
        HMM_Vec3 start = HMM_SubV3(dn.position,
            HMM_MulV3F(cam_right, (total_w - spacing) * 0.5f));

        for (int d = 0; d < num_digits; d++) {
            int digit = digits_str[d] - '0';
            HMM_Vec3 dpos = HMM_AddV3(start, HMM_MulV3F(cam_right, spacing * d));
            draw_digit(out, digit, dpos, cam_right, cam_up, char_w, char_h, color);
        }
    }
}
