#include "magazine_view.h"
#include "game_state.h"
#include "bullet_mods.h"

#include "vendor/imgui/imgui.h"
#include <cstdio>
#include <cmath>

// ============================================================
//  Balatro-style magazine card fan
// ============================================================

// Color for tipping types
static ImU32 tipping_color(Tipping t) {
    switch (t) {
        case Tipping::Hollow_Point:   return IM_COL32(220, 160, 60, 255);  // gold
        case Tipping::Armor_Piercing: return IM_COL32(180, 180, 200, 255); // silver
        case Tipping::Sharpened:      return IM_COL32(200, 80, 80, 255);   // red
        case Tipping::Splitting:      return IM_COL32(100, 200, 100, 255); // green
        default:                      return IM_COL32(100, 100, 110, 255); // grey
    }
}

// Color for enchantment types
static ImU32 enchantment_color(Enchantment e) {
    switch (e) {
        case Enchantment::Incendiary: return IM_COL32(240, 120, 40, 255);  // orange
        case Enchantment::Frost:      return IM_COL32(100, 180, 240, 255); // ice blue
        case Enchantment::Shock:      return IM_COL32(240, 240, 80, 255);  // yellow
        case Enchantment::Vampiric:   return IM_COL32(180, 50, 60, 255);   // blood red
        case Enchantment::Explosive:  return IM_COL32(240, 80, 40, 255);   // fire red
        default:                      return IM_COL32(80, 80, 90, 255);    // dark grey
    }
}

static ImU32 lerp_color(ImU32 a, ImU32 b, float t) {
    int ra = (a >> 0) & 0xFF, ga = (a >> 8) & 0xFF, ba = (a >> 16) & 0xFF, aa = (a >> 24) & 0xFF;
    int rb = (b >> 0) & 0xFF, gb = (b >> 8) & 0xFF, bb = (b >> 16) & 0xFF, ab = (b >> 24) & 0xFF;
    int r = (int)(ra + (rb - ra) * t);
    int g = (int)(ga + (gb - ga) * t);
    int bv = (int)(ba + (bb - ba) * t);
    int av = (int)(aa + (ab - aa) * t);
    return IM_COL32(r, g, bv, av);
}

// Draw a single rounded card with rotation applied via skewed corners
static void draw_card(ImDrawList* draw, ImVec2 center, float card_w, float card_h,
                      float angle, const RoundMod& mod, int round_num,
                      bool is_current_round, bool hovered) {
    // Card corners (relative to center, then rotated)
    float hw = card_w * 0.5f, hh = card_h * 0.5f;
    float cos_a = cosf(angle), sin_a = sinf(angle);

    auto rot = [&](float lx, float ly) -> ImVec2 {
        return ImVec2(center.x + lx * cos_a - ly * sin_a,
                      center.y + lx * sin_a + ly * cos_a);
    };

    ImVec2 tl = rot(-hw, -hh);
    ImVec2 tr = rot( hw, -hh);
    ImVec2 br = rot( hw,  hh);
    ImVec2 bl = rot(-hw,  hh);

    // Card background
    ImU32 bg = hovered ? IM_COL32(65, 65, 75, 245) : IM_COL32(45, 42, 52, 240);
    if (is_current_round) bg = IM_COL32(60, 55, 80, 245);
    draw->AddQuadFilled(tl, tr, br, bl, bg);

    // Card border
    ImU32 border = is_current_round ? IM_COL32(200, 180, 80, 255) : IM_COL32(90, 85, 100, 255);
    if (hovered) border = IM_COL32(220, 210, 160, 255);
    draw->AddQuad(tl, tr, br, bl, border, 1.5f);

    // --- Tipping indicator (top half) ---
    {
        ImVec2 tip_tl = rot(-hw + 3, -hh + 3);
        ImVec2 tip_tr = rot( hw - 3, -hh + 3);
        ImVec2 tip_br = rot( hw - 3, -2);
        ImVec2 tip_bl = rot(-hw + 3, -2);

        ImU32 tip_col = tipping_color(mod.tipping);
        draw->AddQuadFilled(tip_tl, tip_tr, tip_br, tip_bl, tip_col);

        if (mod.tipping != Tipping::None) {
            const char* name = tipping_name(mod.tipping);
            // Short label: first 2 chars
            char label[4] = {name[0], name[1], 0};
            ImVec2 text_pos = rot(-hw + 6, -hh + 5);
            draw->AddText(text_pos, IM_COL32(0, 0, 0, 220), label);
        }
    }

    // --- Enchantment indicator (bottom half) ---
    {
        ImVec2 enc_tl = rot(-hw + 3, 2);
        ImVec2 enc_tr = rot( hw - 3, 2);
        ImVec2 enc_br = rot( hw - 3, hh - 3);
        ImVec2 enc_bl = rot(-hw + 3, hh - 3);

        ImU32 enc_col = enchantment_color(mod.enchantment);
        draw->AddQuadFilled(enc_tl, enc_tr, enc_br, enc_bl, enc_col);

        if (mod.enchantment != Enchantment::None) {
            const char* name = enchantment_name(mod.enchantment);
            char label[4] = {name[0], name[1], 0};
            ImVec2 text_pos = rot(-hw + 6, 4);
            draw->AddText(text_pos, IM_COL32(0, 0, 0, 220), label);
        }
    }

    // --- Round number (small, bottom center) ---
    {
        char num_buf[16];
        snprintf(num_buf, sizeof(num_buf), "%d", round_num + 1);
        ImVec2 num_size = ImGui::CalcTextSize(num_buf);
        ImVec2 text_pos = rot(-num_size.x * 0.5f, hh - 16);
        draw->AddText(text_pos, IM_COL32(180, 175, 190, 200), num_buf);
    }
}

void magazine_view_draw(GameState& gs) {
    if (!gs.show_magazine_view) return;

    Weapon& w = gs.weapons[gs.active_weapon];
    Magazine& mag = w.magazine;
    if (mag.capacity <= 0) return;

    ImGuiIO& io = ImGui::GetIO();
    float screen_w = io.DisplaySize.x;
    float screen_h = io.DisplaySize.y;
    ImDrawList* draw = ImGui::GetForegroundDrawList();

    // --- Dark overlay ---
    draw->AddRectFilled(ImVec2(0, 0), ImVec2(screen_w, screen_h),
                        IM_COL32(0, 0, 0, 120));

    // --- Title ---
    {
        const char* wname = w.config.name;
        char title[128];
        snprintf(title, sizeof(title), "%s  -  Magazine  (%d/%d)", wname, w.ammo, mag.capacity);
        ImVec2 title_size = ImGui::CalcTextSize(title);
        float tx = screen_w * 0.5f - title_size.x * 0.5f;
        draw->AddText(ImVec2(tx, screen_h * 0.22f), IM_COL32(220, 215, 200, 255), title);
    }

    // --- Fan layout ---
    int n = mag.capacity;
    float card_w = 44.0f;
    float card_h = 68.0f;

    // Clamp spacing for large magazines
    float max_total = screen_w * 0.75f;
    float spacing = (n > 1) ? fminf(card_w + 8.0f, max_total / (float)n) : 0.0f;

    float fan_width = spacing * (n - 1);
    float fan_center_x = screen_w * 0.5f;
    float fan_center_y = screen_h * 0.55f;

    // Slight arc
    float max_arc_angle = 0.3f; // radians total spread
    float arc_radius = 600.0f;  // larger = flatter arc

    int current_round = mag.capacity - w.ammo; // index of next round to fire

    ImVec2 mouse = io.MousePos;

    // Draw cards back to front (center cards on top)
    for (int i = 0; i < n; i++) {
        float t = (n > 1) ? ((float)i / (float)(n - 1) - 0.5f) * 2.0f : 0.0f; // -1 to 1

        float cx = fan_center_x + t * fan_width * 0.5f;
        float angle = t * max_arc_angle * 0.5f;
        float arc_y = t * t * arc_radius * 0.02f; // parabolic arc (higher at edges)
        float cy = fan_center_y + arc_y;

        // Hover detection (rough AABB)
        float hw = card_w * 0.5f + 2, hh = card_h * 0.5f + 2;
        bool hovered = (mouse.x >= cx - hw && mouse.x <= cx + hw &&
                        mouse.y >= cy - hh && mouse.y <= cy + hh);

        // Lift hovered card
        if (hovered) cy -= 18.0f;

        bool is_current = (i == current_round);
        bool is_spent = (i < current_round); // already fired this magazine

        RoundMod mod = mag.get(i);

        draw_card(draw, ImVec2(cx, cy), card_w, card_h, angle, mod, i,
                  is_current, hovered);

        // Dim spent rounds
        if (is_spent) {
            float cos_a = cosf(angle), sin_a = sinf(angle);
            auto rot = [&](float lx, float ly) -> ImVec2 {
                return ImVec2(cx + lx * cos_a - ly * sin_a,
                              cy + lx * sin_a + ly * cos_a);
            };
            float chw = card_w * 0.5f, chh = card_h * 0.5f;
            draw->AddQuadFilled(rot(-chw, -chh), rot(chw, -chh),
                                rot(chw, chh), rot(-chw, chh),
                                IM_COL32(0, 0, 0, 140));
        }
    }

    // --- Legend at bottom ---
    {
        float ly = screen_h * 0.78f;
        float lx = screen_w * 0.5f;

        // Tipping legend
        draw->AddText(ImVec2(lx - 200, ly),
                      IM_COL32(200, 160, 80, 255), "Tipping (top)");
        draw->AddText(ImVec2(lx - 200, ly + 18),
                      IM_COL32(160, 155, 170, 200),
                      "HP=Hollow Point  AP=Armor Pierce  Sh=Sharpened  Sp=Splitting");

        // Enchantment legend
        draw->AddText(ImVec2(lx - 200, ly + 42),
                      IM_COL32(120, 90, 200, 255), "Enchantment (bottom)");
        draw->AddText(ImVec2(lx - 200, ly + 60),
                      IM_COL32(160, 155, 170, 200),
                      "In=Incendiary  Fr=Frost  Sh=Shock  Va=Vampiric  Ex=Explosive");
    }

    // --- Tooltip on hover ---
    for (int i = 0; i < n; i++) {
        float t = (n > 1) ? ((float)i / (float)(n - 1) - 0.5f) * 2.0f : 0.0f;
        float cx = fan_center_x + t * fan_width * 0.5f;
        float arc_y = t * t * arc_radius * 0.02f;
        float cy = fan_center_y + arc_y;
        float hw = card_w * 0.5f + 2, hh = card_h * 0.5f + 2;
        bool hovered = (mouse.x >= cx - hw && mouse.x <= cx + hw &&
                        mouse.y >= cy - hh && mouse.y <= cy + hh);
        if (hovered) {
            RoundMod mod = mag.get(i);
            ImGui::SetNextWindowPos(ImVec2(cx + card_w * 0.6f, cy - card_h * 0.3f));
            ImGui::SetNextWindowBgAlpha(0.85f);
            ImGui::Begin("##mag_tooltip", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
            ImGui::Text("Round %d", i + 1);
            ImGui::Separator();
            if (mod.tipping != Tipping::None) {
                ImGui::TextColored(ImVec4(0.9f,0.6f,0.2f,1), "Tip: %s", tipping_name(mod.tipping));
                ImGui::TextColored(ImVec4(0.7f,0.7f,0.7f,1), "  %s", tipping_desc(mod.tipping));
            } else {
                ImGui::TextDisabled("Tip: None");
            }
            if (mod.enchantment != Enchantment::None) {
                ImGui::TextColored(ImVec4(0.5f,0.3f,0.9f,1), "Ench: %s", enchantment_name(mod.enchantment));
                ImGui::TextColored(ImVec4(0.7f,0.7f,0.7f,1), "  %s", enchantment_desc(mod.enchantment));
            } else {
                ImGui::TextDisabled("Ench: None");
            }
            if (i < (mag.capacity - w.ammo))
                ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1), "(spent)");
            else if (i == (mag.capacity - w.ammo))
                ImGui::TextColored(ImVec4(1.0f,0.9f,0.3f,1), ">> NEXT <<");
            ImGui::End();
            break; // only one tooltip
        }
    }

    // --- Dismiss hint ---
    {
        const char* key = input_code_name(gs.kb.get(Action::MagazineView, 0));
        char hint[64];
        snprintf(hint, sizeof(hint), "Press [%s] to close", key);
        ImVec2 hint_size = ImGui::CalcTextSize(hint);
        draw->AddText(ImVec2(screen_w * 0.5f - hint_size.x * 0.5f, screen_h * 0.9f),
                      IM_COL32(150, 145, 160, 180), hint);
    }
}
