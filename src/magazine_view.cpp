#include "magazine_view.h"
#include "game_state.h"
#include "bullet_mods.h"

#include "vendor/imgui/imgui.h"
#include <cstdio>
#include <cmath>

// ============================================================
//  Balatro-style magazine card fan
// ============================================================

static ImU32 tipping_color(Tipping t) {
    switch (t) {
        case Tipping::Hollow_Point:   return IM_COL32(220, 160, 60, 255);
        case Tipping::Armor_Piercing: return IM_COL32(180, 180, 200, 255);
        case Tipping::Sharpened:      return IM_COL32(200, 80, 80, 255);
        case Tipping::Splitting:      return IM_COL32(100, 200, 100, 255);
        default:                      return IM_COL32(100, 100, 110, 255);
    }
}

static ImU32 enchantment_color(Enchantment e) {
    switch (e) {
        case Enchantment::Incendiary: return IM_COL32(240, 120, 40, 255);
        case Enchantment::Frost:      return IM_COL32(100, 180, 240, 255);
        case Enchantment::Shock:      return IM_COL32(240, 240, 80, 255);
        case Enchantment::Vampiric:   return IM_COL32(180, 50, 60, 255);
        case Enchantment::Explosive:  return IM_COL32(240, 80, 40, 255);
        default:                      return IM_COL32(80, 80, 90, 255);
    }
}

static void draw_card(ImDrawList* draw, ImVec2 center, float card_w, float card_h,
                      float angle, const RoundMod& mod, int round_num,
                      bool is_current_round, bool hovered, bool is_spent) {
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

    // Border
    ImU32 border = is_current_round ? IM_COL32(200, 180, 80, 255) : IM_COL32(90, 85, 100, 255);
    if (hovered) border = IM_COL32(220, 210, 160, 255);
    draw->AddQuad(tl, tr, br, bl, border, 1.5f);

    // Tipping indicator (top half)
    {
        ImVec2 tip_tl = rot(-hw + 3, -hh + 3);
        ImVec2 tip_tr = rot( hw - 3, -hh + 3);
        ImVec2 tip_br = rot( hw - 3, -2);
        ImVec2 tip_bl = rot(-hw + 3, -2);
        draw->AddQuadFilled(tip_tl, tip_tr, tip_br, tip_bl, tipping_color(mod.tipping));
        if (mod.tipping != Tipping::None) {
            const char* name = tipping_name(mod.tipping);
            char label[4] = {name[0], name[1], 0};
            draw->AddText(rot(-hw + 6, -hh + 5), IM_COL32(0, 0, 0, 220), label);
        }
    }

    // Enchantment indicator (bottom half)
    {
        ImVec2 enc_tl = rot(-hw + 3, 2);
        ImVec2 enc_tr = rot( hw - 3, 2);
        ImVec2 enc_br = rot( hw - 3, hh - 3);
        ImVec2 enc_bl = rot(-hw + 3, hh - 3);
        draw->AddQuadFilled(enc_tl, enc_tr, enc_br, enc_bl, enchantment_color(mod.enchantment));
        if (mod.enchantment != Enchantment::None) {
            const char* name = enchantment_name(mod.enchantment);
            char label[4] = {name[0], name[1], 0};
            draw->AddText(rot(-hw + 6, 4), IM_COL32(0, 0, 0, 220), label);
        }
    }

    // Round number
    {
        char num_buf[16];
        snprintf(num_buf, sizeof(num_buf), "%d", round_num + 1);
        ImVec2 num_size = ImGui::CalcTextSize(num_buf);
        draw->AddText(rot(-num_size.x * 0.5f, hh - 16), IM_COL32(180, 175, 190, 200), num_buf);
    }

    // Dim spent rounds
    if (is_spent) {
        draw->AddQuadFilled(tl, tr, br, bl, IM_COL32(0, 0, 0, 140));
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

    PendingModApplication& pm = gs.pending_mod;
    bool applying = pm.active && pm.applications_left > 0;

    // --- Dark overlay ---
    draw->AddRectFilled(ImVec2(0, 0), ImVec2(screen_w, screen_h),
                        IM_COL32(0, 0, 0, 120));

    // --- Title ---
    {
        char title[128];
        snprintf(title, sizeof(title), "%s  -  Magazine  (%d/%d)",
                 w.config.name, w.ammo, mag.capacity);
        ImVec2 title_size = ImGui::CalcTextSize(title);
        draw->AddText(ImVec2(screen_w * 0.5f - title_size.x * 0.5f, screen_h * 0.20f),
                      IM_COL32(220, 215, 200, 255), title);
    }

    // --- Application prompt ---
    if (applying) {
        char prompt[128];
        const char* mod_name = pm.is_tipping
            ? tipping_name(pm.tipping)
            : enchantment_name(pm.enchantment);
        snprintf(prompt, sizeof(prompt), "Click %d round%s to apply: %s",
                 pm.applications_left, pm.applications_left > 1 ? "s" : "", mod_name);
        ImVec2 prompt_size = ImGui::CalcTextSize(prompt);
        ImU32 prompt_col = pm.is_tipping
            ? IM_COL32(220, 160, 60, 255)
            : IM_COL32(120, 90, 200, 255);
        draw->AddText(ImVec2(screen_w * 0.5f - prompt_size.x * 0.5f, screen_h * 0.25f),
                      prompt_col, prompt);
    }

    // --- Fan layout ---
    int n = mag.capacity;
    float card_w = 44.0f;
    float card_h = 68.0f;

    float max_total = screen_w * 0.75f;
    float spacing = (n > 1) ? fminf(card_w + 8.0f, max_total / (float)n) : 0.0f;
    float fan_width = spacing * (n - 1);
    float fan_center_x = screen_w * 0.5f;
    float fan_center_y = screen_h * 0.50f;
    float max_arc_angle = 0.3f;
    float arc_radius = 600.0f;

    int current_round = mag.capacity - w.ammo;
    ImVec2 mouse = io.MousePos;
    bool mouse_clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);

    // --- Draw cards + handle interaction ---
    int hovered_card = -1;

    // First pass: find hovered card
    for (int i = 0; i < n; i++) {
        float t = (n > 1) ? ((float)i / (float)(n - 1) - 0.5f) * 2.0f : 0.0f;
        float cx = fan_center_x + t * fan_width * 0.5f;
        float arc_y = t * t * arc_radius * 0.02f;
        float cy = fan_center_y + arc_y;

        float hw = card_w * 0.5f + 2, hh = card_h * 0.5f + 2;
        if (mouse.x >= cx - hw && mouse.x <= cx + hw &&
            mouse.y >= cy - hh && mouse.y <= cy + hh) {
            hovered_card = i;
        }
    }

    // Second pass: draw
    for (int i = 0; i < n; i++) {
        float t = (n > 1) ? ((float)i / (float)(n - 1) - 0.5f) * 2.0f : 0.0f;
        float cx = fan_center_x + t * fan_width * 0.5f;
        float angle = t * max_arc_angle * 0.5f;
        float arc_y = t * t * arc_radius * 0.02f;
        float cy = fan_center_y + arc_y;

        bool hovered = (i == hovered_card);
        bool is_spent = (i < current_round);
        bool is_current = (i == current_round);

        if (hovered) cy -= 18.0f;

        // Highlight clickable cards during mod application
        if (applying && hovered) {
            cy -= 4.0f; // extra lift for clickable cards
        }

        RoundMod mod = mag.get(i);
        draw_card(draw, ImVec2(cx, cy), card_w, card_h, angle, mod, i,
                  is_current, hovered, is_spent);

        // Applying glow effect on clickable cards
        if (applying && hovered) {
            float cos_a = cosf(angle), sin_a = sinf(angle);
            auto rot = [&](float lx, float ly) -> ImVec2 {
                return ImVec2(cx + lx * cos_a - ly * sin_a,
                              cy + lx * sin_a + ly * cos_a);
            };
            float chw = card_w * 0.5f, chh = card_h * 0.5f;
            ImU32 glow = pm.is_tipping
                ? IM_COL32(220, 160, 60, 60)
                : IM_COL32(120, 90, 200, 60);
            draw->AddQuadFilled(rot(-chw, -chh), rot(chw, -chh),
                                rot(chw, chh), rot(-chw, chh), glow);
        }
    }

    // --- Handle click to apply mod (spent rounds included) ---
    if (applying && mouse_clicked && hovered_card >= 0) {
        if (pm.is_tipping) {
            mag.set_tipping(hovered_card, pm.tipping);
            printf("Applied %s to round %d\n", tipping_name(pm.tipping), hovered_card + 1);
        } else {
            mag.set_enchantment(hovered_card, pm.enchantment);
            printf("Applied %s to round %d\n", enchantment_name(pm.enchantment), hovered_card + 1);
        }
        pm.applications_left--;
        if (pm.applications_left <= 0) {
            pm.active = false;
            // Keep magazine view open so player can see results
        }
    }

    // --- Tooltip on hover ---
    if (hovered_card >= 0) {
        int i = hovered_card;
        float t = (n > 1) ? ((float)i / (float)(n - 1) - 0.5f) * 2.0f : 0.0f;
        float cx = fan_center_x + t * fan_width * 0.5f;

        RoundMod mod = mag.get(i);

        ImGui::SetNextWindowPos(ImVec2(cx + card_w * 0.6f, fan_center_y - card_h * 0.5f));
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
        if (i < current_round)
            ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1), "(spent)");
        else if (i == current_round)
            ImGui::TextColored(ImVec4(1.0f,0.9f,0.3f,1), ">> NEXT <<");

        if (applying) {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.3f,1.0f,0.3f,1), "Click to apply mod");
        }
        ImGui::End();
    }

    // --- Legend ---
    {
        float ly = screen_h * 0.75f;
        float lx = screen_w * 0.5f;
        draw->AddText(ImVec2(lx - 200, ly),
                      IM_COL32(200, 160, 80, 255), "Tipping (top)");
        draw->AddText(ImVec2(lx - 200, ly + 18),
                      IM_COL32(160, 155, 170, 200),
                      "HP=Hollow Point  AP=Armor Pierce  Sh=Sharpened  Sp=Splitting");
        draw->AddText(ImVec2(lx - 200, ly + 42),
                      IM_COL32(120, 90, 200, 255), "Enchantment (bottom)");
        draw->AddText(ImVec2(lx - 200, ly + 60),
                      IM_COL32(160, 155, 170, 200),
                      "In=Incendiary  Fr=Frost  Sh=Shock  Va=Vampiric  Ex=Explosive");
    }

    // --- Dismiss hint ---
    {
        const char* key = input_code_name(gs.kb.get(Action::MagazineView, 0));
        char hint[64];
        if (applying)
            snprintf(hint, sizeof(hint), "Select rounds (%d remaining)", pm.applications_left);
        else
            snprintf(hint, sizeof(hint), "Press [%s] to close", key);
        ImVec2 hint_size = ImGui::CalcTextSize(hint);
        draw->AddText(ImVec2(screen_w * 0.5f - hint_size.x * 0.5f, screen_h * 0.88f),
                      IM_COL32(150, 145, 160, 180), hint);
    }
}
