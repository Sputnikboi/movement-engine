#include "damage_numbers.h"
#include "vendor/imgui/imgui.h"
#include <cstdio>
#include <cmath>

// ============================================================
//  Spawn / Update
// ============================================================

static int find_free_slot(DamageNumber* numbers, int max_n) {
    int best = -1;
    float oldest_life = 999.0f;
    for (int i = 0; i < max_n; i++) {
        if (!numbers[i].active) { best = i; break; }
        if (numbers[i].lifetime < oldest_life) {
            oldest_life = numbers[i].lifetime;
            best = i;
        }
    }
    return (best < 0) ? 0 : best;
}

void DamageNumberSystem::spawn(HMM_Vec3 pos, int damage, int entity_id, bool is_kill, bool is_poison) {
    // Try to stack onto an existing number for the same entity and type
    if (entity_id >= 0) {
        for (int i = 0; i < MAX_NUMBERS; i++) {
            DamageNumber& dn = numbers[i];
            if (!dn.active) continue;
            if (dn.entity_id != entity_id) continue;
            if (dn.is_poison != is_poison) continue;
            if (!is_poison && (dn.max_lifetime - dn.lifetime) > STACK_WINDOW) continue;

            // Stack: add damage, refresh lifetime and position
            dn.value += damage;
            dn.lifetime = dn.max_lifetime;
            dn.velocity_y = is_poison ? 0.3f : 1.8f;
            dn.position = pos;
            if (is_kill) dn.is_kill = true;
            return;
        }
    }

    int best = find_free_slot(numbers, MAX_NUMBERS);
    DamageNumber& dn = numbers[best];
    dn.position = pos;
    dn.velocity_y = is_poison ? 0.3f : 1.8f;
    dn.max_lifetime = is_poison ? 999.0f : 0.9f; // poison persists until dismissed
    dn.lifetime = dn.max_lifetime;
    dn.value = damage;
    dn.value_accum = 0.0f;
    dn.entity_id = entity_id;
    dn.is_kill = is_kill;
    dn.is_poison = is_poison;
    dn.active = true;
}

void DamageNumberSystem::spawn_float(HMM_Vec3 pos, float damage, int entity_id, bool is_kill, bool is_poison) {
    // Try to stack onto an existing number for the same entity and type
    if (entity_id >= 0) {
        for (int i = 0; i < MAX_NUMBERS; i++) {
            DamageNumber& dn = numbers[i];
            if (!dn.active) continue;
            if (dn.entity_id != entity_id) continue;
            if (dn.is_poison != is_poison) continue;
            if (!is_poison && (dn.max_lifetime - dn.lifetime) > STACK_WINDOW) continue;

            // Accumulate sub-integer damage
            dn.value_accum += damage;
            int whole = (int)dn.value_accum;
            if (whole > 0) {
                dn.value += whole;
                dn.value_accum -= (float)whole;
            }
            dn.lifetime = dn.max_lifetime;
            dn.velocity_y = is_poison ? 0.3f : 1.8f;
            dn.position = pos;
            if (is_kill) dn.is_kill = true;
            return;
        }
    }

    // New number
    int best = find_free_slot(numbers, MAX_NUMBERS);
    DamageNumber& dn = numbers[best];
    dn.position = pos;
    dn.velocity_y = is_poison ? 0.3f : 1.8f;
    dn.max_lifetime = is_poison ? 999.0f : 0.9f;
    dn.lifetime = dn.max_lifetime;
    dn.value_accum = damage;
    int whole = (int)dn.value_accum;
    dn.value = whole;
    dn.value_accum -= (float)whole;
    dn.entity_id = entity_id;
    dn.is_kill = is_kill;
    dn.is_poison = is_poison;
    dn.active = true;
}

void DamageNumberSystem::dismiss_poison(int entity_id) {
    for (int i = 0; i < MAX_NUMBERS; i++) {
        DamageNumber& dn = numbers[i];
        if (!dn.active || !dn.is_poison) continue;
        if (dn.entity_id != entity_id) continue;
        // Start a short fade-out instead of instant removal
        dn.max_lifetime = 0.6f;
        dn.lifetime = 0.6f;
    }
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
//  Screen-space UI rendering
// ============================================================

// Project world position to screen coords. Returns false if behind camera.
static bool world_to_screen(HMM_Vec3 world_pos, HMM_Mat4 view_proj,
                            float screen_w, float screen_h,
                            float& out_x, float& out_y) {
    HMM_Vec4 clip = HMM_MulM4V4(view_proj, HMM_V4(world_pos.X, world_pos.Y, world_pos.Z, 1.0f));

    // Behind camera
    if (clip.W <= 0.001f) return false;

    float inv_w = 1.0f / clip.W;
    float ndc_x = clip.X * inv_w;
    float ndc_y = clip.Y * inv_w;

    // NDC to screen (Vulkan NDC: X [-1,1] left-right, Y [-1,1] top-bottom)
    out_x = (ndc_x * 0.5f + 0.5f) * screen_w;
    out_y = (ndc_y * 0.5f + 0.5f) * screen_h;  // proj already flips Y for Vulkan

    return true;
}

void DamageNumberSystem::draw_ui(HMM_Mat4 view_proj, float screen_w, float screen_h, ImFont* custom_font) const {
    ImDrawList* draw = ImGui::GetForegroundDrawList();

    for (int i = 0; i < MAX_NUMBERS; i++) {
        const DamageNumber& dn = numbers[i];
        if (!dn.active) continue;
        if (dn.value <= 0 && dn.is_poison) continue; // don't show "0"

        float sx, sy;
        if (!world_to_screen(dn.position, view_proj, screen_w, screen_h, sx, sy))
            continue;

        // Off screen? Skip
        if (sx < -50 || sx > screen_w + 50 || sy < -50 || sy > screen_h + 50)
            continue;

        // Alpha: poison numbers stay fully opaque until dismissed (fade-out via dismiss_poison)
        float alpha;
        if (dn.is_poison && dn.max_lifetime > 10.0f) {
            alpha = 1.0f; // persistent poison number -- fully opaque
        } else {
            float t = 1.0f - dn.lifetime / dn.max_lifetime; // 0 -> 1
            alpha = (t < 0.6f) ? 1.0f : (1.0f - (t - 0.6f) / 0.4f); // fade last 40%
        }

        // Color
        ImU32 color;
        if (dn.is_kill) {
            color = ImGui::ColorConvertFloat4ToU32(
                ImVec4(1.0f, 0.25f, 0.1f, alpha));
        } else if (dn.is_poison) {
            color = ImGui::ColorConvertFloat4ToU32(
                ImVec4(0.3f, 0.9f, 0.3f, alpha));
        } else {
            color = ImGui::ColorConvertFloat4ToU32(
                ImVec4(1.0f, 1.0f, 0.85f, alpha));
        }

        // Shadow/outline color
        ImU32 shadow = ImGui::ColorConvertFloat4ToU32(
            ImVec4(0.0f, 0.0f, 0.0f, alpha * 0.7f));

        // Format text
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", dn.value);

        // Font size -- slightly larger for kills
        float font_size = dn.is_kill ? 24.0f : 20.0f;
        // Slight upward drift on screen (in addition to world drift)
        float screen_offset_y = dn.is_poison ? 0.0f : -(1.0f - dn.lifetime / dn.max_lifetime) * 15.0f;

        ImFont* font = custom_font ? custom_font : ImGui::GetFont();
        ImVec2 text_size = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, buf);
        float tx = sx - text_size.x * 0.5f;
        float ty = sy - text_size.y * 0.5f + screen_offset_y;

        // Draw shadow (offset by 1px in each direction for outline effect)
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                if (dx == 0 && dy == 0) continue;
                draw->AddText(font, font_size,
                    ImVec2(tx + dx, ty + dy), shadow, buf);
            }
        }

        // Draw text
        draw->AddText(font, font_size, ImVec2(tx, ty), color, buf);
    }
}
