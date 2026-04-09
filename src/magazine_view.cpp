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
        case Tipping::Sharpened:      return IM_COL32(200, 80, 80, 255);
        case Tipping::Piercing:       return IM_COL32(180, 180, 200, 255);
        case Tipping::Crystal_Tipped: return IM_COL32(140, 200, 240, 255);
        case Tipping::Aerodynamic:    return IM_COL32(100, 220, 180, 255);
        case Tipping::Poison_Tipped:  return IM_COL32(80, 200, 80, 255);
        case Tipping::Blank:          return IM_COL32(60, 60, 60, 255);
        case Tipping::Split:          return IM_COL32(220, 180, 60, 255);
        case Tipping::Serrated:       return IM_COL32(200, 60, 60, 255);
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
                      bool is_current_round, bool hovered, bool is_spent,
                      bool is_selected) {
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
    if (is_selected) bg = IM_COL32(50, 70, 50, 245);
    draw->AddQuadFilled(tl, tr, br, bl, bg);

    // Border
    ImU32 border = is_current_round ? IM_COL32(200, 180, 80, 255) : IM_COL32(90, 85, 100, 255);
    if (is_selected) border = IM_COL32(80, 255, 80, 255);
    else if (hovered) border = IM_COL32(220, 210, 160, 255);
    float border_thick = is_selected ? 2.5f : 1.5f;
    draw->AddQuad(tl, tr, br, bl, border, border_thick);

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

    // Selection checkmark
    if (is_selected) {
        draw->AddText(rot(hw - 16, -hh + 3), IM_COL32(80, 255, 80, 255), "OK");
    }
}

// Persistent drag state for reordering
static int drag_source = -1;
static bool dragging = false;
static bool drag_pending = false;  // clicked but haven't moved enough yet
static ImVec2 drag_start = {};
static constexpr float DRAG_DEAD_ZONE = 6.0f;

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
    bool applying = pm.active;
    bool in_shop = gs.in_shop_room;
    bool can_reorder = in_shop;

    // --- Dark overlay ---
    draw->AddRectFilled(ImVec2(0, 0), ImVec2(screen_w, screen_h),
                        IM_COL32(0, 0, 0, 120));

    // --- Title ---
    {
        char title[128];
        snprintf(title, sizeof(title), "%s  -  Magazine  (%d/%d)",
                 w.config.name, w.ammo, mag.capacity);
        ImVec2 title_size = ImGui::CalcTextSize(title);
        draw->AddText(ImVec2(screen_w * 0.5f - title_size.x * 0.5f, screen_h * 0.15f),
                      IM_COL32(220, 215, 200, 255), title);
    }

    // --- Mod info panel (when applying) ---
    if (applying) {
        const char* mod_name = pm.is_tipping
            ? tipping_name(pm.tipping) : enchantment_name(pm.enchantment);
        const char* mod_desc = pm.is_tipping
            ? tipping_desc(pm.tipping) : enchantment_desc(pm.enchantment);
        ImU32 mod_col = pm.is_tipping
            ? IM_COL32(220, 160, 60, 255) : IM_COL32(120, 90, 200, 255);

        char info[128];
        snprintf(info, sizeof(info), "Applying: %s", mod_name);
        ImVec2 info_size = ImGui::CalcTextSize(info);
        draw->AddText(ImVec2(screen_w * 0.5f - info_size.x * 0.5f, screen_h * 0.20f),
                      mod_col, info);

        ImVec2 desc_size = ImGui::CalcTextSize(mod_desc);
        draw->AddText(ImVec2(screen_w * 0.5f - desc_size.x * 0.5f, screen_h * 0.20f + 20),
                      IM_COL32(180, 180, 180, 255), mod_desc);

        char slots[64];
        snprintf(slots, sizeof(slots), "Select up to %d round%s  (%d/%d selected)",
                 pm.max_applications, pm.max_applications > 1 ? "s" : "",
                 pm.selected_count, pm.max_applications);
        ImVec2 slots_size = ImGui::CalcTextSize(slots);
        draw->AddText(ImVec2(screen_w * 0.5f - slots_size.x * 0.5f, screen_h * 0.20f + 42),
                      IM_COL32(160, 160, 170, 255), slots);
    }

    // --- Reorder hint (only when not applying, since applying has its own info panel) ---
    if (can_reorder && !applying) {
        const char* reorder_hint = "Drag cards to reorder";
        ImVec2 rh_size = ImGui::CalcTextSize(reorder_hint);
        draw->AddText(ImVec2(screen_w * 0.5f - rh_size.x * 0.5f, screen_h * 0.20f),
                      IM_COL32(150, 200, 150, 200), reorder_hint);
    }

    // --- Fan layout ---
    int n = mag.capacity;
    float card_w = 44.0f;
    float card_h = 68.0f;

    float max_total = screen_w * 0.75f;
    float spacing_v = (n > 1) ? fminf(card_w + 8.0f, max_total / (float)n) : 0.0f;
    float fan_width = spacing_v * (n - 1);
    float fan_center_x = screen_w * 0.5f;
    float fan_center_y = screen_h * 0.45f;
    float max_arc_angle = 0.3f;
    float arc_radius = 600.0f;

    int current_round = w.config.infinite_ammo ? w.current_round : (mag.capacity - w.ammo);
    ImVec2 mouse = io.MousePos;
    bool mouse_clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    bool mouse_down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    bool mouse_released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);

    auto card_cx = [&](int i) -> float {
        float t = (n > 1) ? ((float)i / (float)(n - 1) - 0.5f) * 2.0f : 0.0f;
        return fan_center_x + t * fan_width * 0.5f;
    };
    auto card_cy = [&](int i) -> float {
        float t = (n > 1) ? ((float)i / (float)(n - 1) - 0.5f) * 2.0f : 0.0f;
        return fan_center_y + t * t * arc_radius * 0.02f;
    };

    // --- Find hovered card ---
    int hovered_card = -1;
    for (int i = 0; i < n; i++) {
        float cx = card_cx(i);
        float cy = card_cy(i);
        float hw = card_w * 0.5f + 2, hh = card_h * 0.5f + 2;
        if (mouse.x >= cx - hw && mouse.x <= cx + hw &&
            mouse.y >= cy - hh && mouse.y <= cy + hh) {
            hovered_card = i;
        }
    }

    // --- Drag reorder logic (shop only) ---
    // Uses a dead zone so clicks don't immediately start drags.
    if (can_reorder) {
        if (mouse_clicked && hovered_card >= 0) {
            drag_source = hovered_card;
            drag_pending = true;
            dragging = false;
            drag_start = mouse;
        }
        // Promote pending to actual drag once mouse moves past dead zone
        if (drag_pending && mouse_down && !dragging) {
            float dx = mouse.x - drag_start.x;
            float dy = mouse.y - drag_start.y;
            if (dx * dx + dy * dy > DRAG_DEAD_ZONE * DRAG_DEAD_ZONE) {
                dragging = true;
            }
        }
        if ((dragging || drag_pending) && mouse_released) {
            if (dragging && drag_source >= 0 && hovered_card >= 0 && drag_source != hovered_card) {
                mag.swap(drag_source, hovered_card);
                printf("Swapped round %d <-> %d\n", drag_source + 1, hovered_card + 1);
            }
            drag_source = -1;
            dragging = false;
            drag_pending = false;
        }
        if (!mouse_down) {
            drag_source = -1;
            dragging = false;
            drag_pending = false;
        }
    } else {
        drag_source = -1;
        dragging = false;
        drag_pending = false;
    }

    // --- Draw cards (skip drag source — drawn at cursor below) ---
    for (int i = 0; i < n; i++) {
        if (dragging && i == drag_source) continue;

        float t = (n > 1) ? ((float)i / (float)(n - 1) - 0.5f) * 2.0f : 0.0f;
        float cx = card_cx(i);
        float angle = t * max_arc_angle * 0.5f;
        float cy = card_cy(i);

        bool hovered = (i == hovered_card);
        bool is_spent = (i < current_round);
        bool is_current = (i == current_round);
        bool is_selected = applying && pm.selected[i];

        if (hovered) cy -= 18.0f;
        if (is_selected) cy -= 6.0f;

        RoundMod mod = mag.get(i);
        draw_card(draw, ImVec2(cx, cy), card_w, card_h, angle, mod, i,
                  is_current, hovered, is_spent, is_selected);

        // Hover glow when applying
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

        // Drop target indicator
        if (dragging && hovered && i != drag_source) {
            float cos_a = cosf(angle), sin_a = sinf(angle);
            auto rot = [&](float lx, float ly) -> ImVec2 {
                return ImVec2(cx + lx * cos_a - ly * sin_a,
                              cy + lx * sin_a + ly * cos_a);
            };
            float chw = card_w * 0.5f, chh = card_h * 0.5f;
            draw->AddQuad(rot(-chw, -chh), rot(chw, -chh),
                          rot(chw, chh), rot(-chw, chh),
                          IM_COL32(100, 200, 255, 180), 2.0f);
        }
    }

    // --- Draw dragged card floating at mouse cursor ---
    if (dragging && drag_source >= 0 && drag_source < n) {
        int i = drag_source;
        bool is_spent = (i < current_round);
        bool is_current = (i == current_round);
        bool is_selected = applying && pm.selected[i];
        RoundMod mod = mag.get(i);
        draw_card(draw, ImVec2(mouse.x, mouse.y), card_w, card_h, 0.0f, mod, i,
                  is_current, true, is_spent, is_selected);
    }

    // --- Handle click to toggle selection (applying mode) ---
    // Use mouse_released so drag-and-drop doesn't accidentally toggle
    static bool was_drag = false;
    if (dragging) was_drag = true;
    if (applying && mouse_released && hovered_card >= 0 && !was_drag) {
        // Don't toggle if click was on Apply/Cancel buttons
        float btn_w = 100.0f;
        float btn_h = 30.0f;
        float btn_gap = 20.0f;
        float btn_y = screen_h * 0.70f;
        float total_btn_w = btn_w * 2 + btn_gap;
        float btn_x = screen_w * 0.5f - total_btn_w * 0.5f;
        bool on_buttons = (mouse.y >= btn_y && mouse.y <= btn_y + btn_h &&
                           mouse.x >= btn_x && mouse.x <= btn_x + total_btn_w);
        if (!on_buttons) {
            pm.toggle(hovered_card);
        }
    }
    if (mouse_released) was_drag = false;

    // --- Tooltip on hover ---
    if (hovered_card >= 0) {
        int i = hovered_card;
        float cx = card_cx(i);

        RoundMod mod = mag.get(i);

        struct TipLine { const char* text; ImU32 color; };
        char line_round[32];
        char line_tip[64], line_tip_desc[96];
        char line_ench[64], line_ench_desc[96];

        TipLine lines[8];
        int lc = 0;

        snprintf(line_round, sizeof(line_round), "Round %d", i + 1);
        lines[lc++] = {line_round, IM_COL32(220, 215, 200, 255)};

        if (mod.tipping != Tipping::None) {
            snprintf(line_tip, sizeof(line_tip), "Tip: %s", tipping_name(mod.tipping));
            snprintf(line_tip_desc, sizeof(line_tip_desc), "  %s", tipping_desc(mod.tipping));
            lines[lc++] = {line_tip, IM_COL32(230, 155, 50, 255)};
            lines[lc++] = {line_tip_desc, IM_COL32(180, 180, 180, 255)};
        } else {
            lines[lc++] = {"Tip: None", IM_COL32(120, 120, 120, 255)};
        }

        if (mod.enchantment != Enchantment::None) {
            snprintf(line_ench, sizeof(line_ench), "Ench: %s", enchantment_name(mod.enchantment));
            snprintf(line_ench_desc, sizeof(line_ench_desc), "  %s", enchantment_desc(mod.enchantment));
            lines[lc++] = {line_ench, IM_COL32(130, 75, 230, 255)};
            lines[lc++] = {line_ench_desc, IM_COL32(180, 180, 180, 255)};
        } else {
            lines[lc++] = {"Ench: None", IM_COL32(120, 120, 120, 255)};
        }

        if (i < current_round)
            lines[lc++] = {"(spent)", IM_COL32(130, 130, 130, 255)};
        else if (i == current_round)
            lines[lc++] = {">> NEXT <<", IM_COL32(255, 230, 75, 255)};

        if (applying && pm.selected[i])
            lines[lc++] = {"[Selected]", IM_COL32(80, 255, 80, 255)};
        else if (applying)
            lines[lc++] = {"Click to select", IM_COL32(75, 255, 75, 255)};

        float pad = 8.0f;
        float line_h = ImGui::GetTextLineHeightWithSpacing();
        float tip_w = 0.0f;
        for (int l = 0; l < lc; l++) {
            ImVec2 sz = ImGui::CalcTextSize(lines[l].text);
            if (sz.x > tip_w) tip_w = sz.x;
        }
        tip_w += pad * 2;
        float tip_h = line_h * lc + pad * 2;

        float tx = cx + card_w * 0.6f;
        float ty = fan_center_y - tip_h * 0.5f;
        if (tx + tip_w > screen_w - 4) tx = cx - card_w * 0.6f - tip_w;
        if (ty < 4) ty = 4;
        if (ty + tip_h > screen_h - 4) ty = screen_h - 4 - tip_h;

        draw->AddRectFilled(ImVec2(tx, ty), ImVec2(tx + tip_w, ty + tip_h),
                            IM_COL32(30, 28, 35, 216), 4.0f);
        draw->AddRect(ImVec2(tx, ty), ImVec2(tx + tip_w, ty + tip_h),
                      IM_COL32(90, 85, 100, 255), 4.0f);

        float ly = ty + pad;
        for (int l = 0; l < lc; l++) {
            draw->AddText(ImVec2(tx + pad, ly), lines[l].color, lines[l].text);
            ly += line_h;
        }
    }

    // --- Apply / Cancel buttons (when applying) ---
    if (applying) {
        float btn_w = 100.0f;
        float btn_h = 30.0f;
        float btn_gap = 20.0f;
        float btn_y = screen_h * 0.70f;
        float total_btn_w = btn_w * 2 + btn_gap;
        float btn_x = screen_w * 0.5f - total_btn_w * 0.5f;

        // Apply button
        {
            bool can_apply = (pm.selected_count > 0);
            ImVec2 atl(btn_x, btn_y);
            ImVec2 abr(btn_x + btn_w, btn_y + btn_h);
            ImU32 abg = can_apply ? IM_COL32(40, 120, 40, 230) : IM_COL32(50, 50, 50, 180);
            ImU32 aborder = can_apply ? IM_COL32(80, 220, 80, 255) : IM_COL32(80, 80, 80, 200);

            bool apply_hovered = (mouse.x >= atl.x && mouse.x <= abr.x &&
                                  mouse.y >= atl.y && mouse.y <= abr.y);
            if (apply_hovered && can_apply)
                abg = IM_COL32(55, 160, 55, 240);

            draw->AddRectFilled(atl, abr, abg, 4.0f);
            draw->AddRect(atl, abr, aborder, 4.0f);
            const char* apply_text = "Apply";
            ImVec2 atsz = ImGui::CalcTextSize(apply_text);
            ImU32 atcol = can_apply ? IM_COL32(220, 255, 220, 255) : IM_COL32(120, 120, 120, 200);
            draw->AddText(ImVec2(atl.x + (btn_w - atsz.x) * 0.5f, atl.y + (btn_h - atsz.y) * 0.5f),
                          atcol, apply_text);

            if (mouse_clicked && apply_hovered && can_apply) {
                for (int j = 0; j < mag.capacity; j++) {
                    if (pm.selected[j]) {
                        if (pm.is_tipping) {
                            mag.set_tipping(j, pm.tipping);
                            printf("Applied %s to round %d\n", tipping_name(pm.tipping), j + 1);
                        } else {
                            mag.set_enchantment(j, pm.enchantment);
                            printf("Applied %s to round %d\n", enchantment_name(pm.enchantment), j + 1);
                        }
                    }
                }
                if (gs.pending_stand_idx >= 0 &&
                    gs.pending_stand_idx < (int)gs.shop_data.stands.size()) {
                    gs.shop_data.stands[gs.pending_stand_idx].purchased = true;
                }
                pm = {};
                gs.pending_stand_idx = -1;
            }
        }

        // Cancel button
        {
            float cx2 = btn_x + btn_w + btn_gap;
            ImVec2 ctl(cx2, btn_y);
            ImVec2 cbr(cx2 + btn_w, btn_y + btn_h);
            ImU32 cbg = IM_COL32(120, 40, 40, 230);
            ImU32 cborder = IM_COL32(220, 80, 80, 255);

            bool cancel_hovered = (mouse.x >= ctl.x && mouse.x <= cbr.x &&
                                   mouse.y >= ctl.y && mouse.y <= cbr.y);
            if (cancel_hovered)
                cbg = IM_COL32(160, 55, 55, 240);

            draw->AddRectFilled(ctl, cbr, cbg, 4.0f);
            draw->AddRect(ctl, cbr, cborder, 4.0f);
            const char* cancel_text = "Cancel";
            ImVec2 ctsz = ImGui::CalcTextSize(cancel_text);
            draw->AddText(ImVec2(ctl.x + (btn_w - ctsz.x) * 0.5f, ctl.y + (btn_h - ctsz.y) * 0.5f),
                          IM_COL32(255, 200, 200, 255), cancel_text);

            if (mouse_clicked && cancel_hovered) {
                gs.currency += pm.cost;
                printf("Cancelled mod application -- refunded %d gold\n", pm.cost);
                pm = {};
                gs.pending_stand_idx = -1;
            }
        }
    }

    // --- Legend ---
    {
        float ly = screen_h * 0.78f;
        float lx = screen_w * 0.5f;
        draw->AddText(ImVec2(lx - 200, ly),
                      IM_COL32(200, 160, 80, 255), "Tipping (top)");
        draw->AddText(ImVec2(lx - 200, ly + 18),
                      IM_COL32(160, 155, 170, 200),
                      "Sh=Sharpened  Pi=Piercing  Cr=Crystal  Ae=Aerodynamic  Po=Poison  Bl=Blank  Sp=Split  Se=Serrated");
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
            snprintf(hint, sizeof(hint), "Select rounds, then Apply or Cancel");
        else
            snprintf(hint, sizeof(hint), "Press [%s] to close", key);
        ImVec2 hint_size = ImGui::CalcTextSize(hint);
        draw->AddText(ImVec2(screen_w * 0.5f - hint_size.x * 0.5f, screen_h * 0.90f),
                      IM_COL32(150, 145, 160, 180), hint);
    }
}
