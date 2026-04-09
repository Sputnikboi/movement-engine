#include "hud.h"
#include "game_state.h"

#include "vendor/imgui/imgui.h"

#include <cstdio>
#include <cmath>

// ============================================================
//  Helper: draw text with a dark shadow/outline
// ============================================================
static void draw_text_shadowed(ImDrawList* dl, ImFont* font, float size,
                               ImVec2 pos, ImU32 color, const char* text) {
    ImU32 shadow = IM_COL32(0, 0, 0, 180);
    // 1px outline
    dl->AddText(font, size, ImVec2(pos.x - 1, pos.y - 1), shadow, text);
    dl->AddText(font, size, ImVec2(pos.x + 1, pos.y - 1), shadow, text);
    dl->AddText(font, size, ImVec2(pos.x - 1, pos.y + 1), shadow, text);
    dl->AddText(font, size, ImVec2(pos.x + 1, pos.y + 1), shadow, text);
    dl->AddText(font, size, pos, color, text);
}

// ============================================================
//  Clean game HUD (raw draw calls, Daydream font)
// ============================================================
void hud_draw_game(GameState& gs, const HudContext& ctx, ImFont* font, ImFont* font_large) {
    if (!font || !font_large) return;
    if (gs.show_settings) return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImGuiIO& io = ImGui::GetIO();
    float sw = io.DisplaySize.x;
    float sh = io.DisplaySize.y;

    // ---- HP bar (top center) ----
    {
        float bar_w = 300.0f;
        float bar_h = 22.0f;
        float bar_x = (sw - bar_w) * 0.5f;
        float bar_y = 20.0f;
        float hp_frac = gs.player.health / gs.player.max_health;
        if (hp_frac < 0.0f) hp_frac = 0.0f;
        if (hp_frac > 1.0f) hp_frac = 1.0f;

        // Background
        dl->AddRectFilled(ImVec2(bar_x - 2, bar_y - 2),
                          ImVec2(bar_x + bar_w + 2, bar_y + bar_h + 2),
                          IM_COL32(0, 0, 0, 180), 4.0f);

        // Fill color: green -> yellow -> red
        ImU32 fill_col;
        if (hp_frac > 0.5f) {
            float t = (hp_frac - 0.5f) * 2.0f;
            fill_col = IM_COL32((int)(60 * (1.0f - t) + 40 * t),
                                (int)(220 * t + 180 * (1.0f - t)),
                                40, 220);
        } else if (hp_frac > 0.25f) {
            float t = (hp_frac - 0.25f) * 4.0f;
            fill_col = IM_COL32((int)(220 + 35 * (1.0f - t)),
                                (int)(180 * t + 60 * (1.0f - t)),
                                30, 220);
        } else {
            fill_col = IM_COL32(220, 40, 30, 220);
        }

        float fill_w = bar_w * hp_frac;
        if (fill_w > 0.5f) {
            dl->AddRectFilled(ImVec2(bar_x, bar_y),
                              ImVec2(bar_x + fill_w, bar_y + bar_h),
                              fill_col, 3.0f);
        }

        // Border
        dl->AddRect(ImVec2(bar_x, bar_y),
                    ImVec2(bar_x + bar_w, bar_y + bar_h),
                    IM_COL32(200, 200, 200, 160), 3.0f, 0, 1.5f);

        // HP text centered on bar
        char hp_text[32];
        snprintf(hp_text, sizeof(hp_text), "%.0f / %.0f", gs.player.health, gs.player.max_health);
        ImVec2 text_sz = font->CalcTextSizeA(21.0f, FLT_MAX, 0.0f, hp_text);
        float tx = bar_x + (bar_w - text_sz.x) * 0.5f;
        float ty = bar_y + (bar_h - text_sz.y) * 0.5f;
        draw_text_shadowed(dl, font, 21.0f, ImVec2(tx, ty), IM_COL32(255, 255, 255, 240), hp_text);
    }

    // ---- Ammo display (bottom left) ----
    {
        Weapon& w = gs.weapons[gs.active_weapon];
        float ax = 30.0f;
        float ay = sh - 80.0f;

        // Weapon name
        draw_text_shadowed(dl, font, 21.0f, ImVec2(ax, ay),
                           IM_COL32(180, 190, 200, 220), w.config.name);

        // Ammo count (large)
        if (w.config.infinite_ammo) {
            // Draw infinity symbol as two overlapping circles
            float cx = ax + 28.0f;
            float cy = ay + 24.0f + 21.0f;
            float r = 10.0f;
            float sep = 9.0f;
            ImU32 shadow = IM_COL32(0, 0, 0, 180);
            ImU32 col = IM_COL32(255, 255, 255, 255);
            dl->AddCircle(ImVec2(cx - sep + 1, cy + 1), r, shadow, 0, 3.0f);
            dl->AddCircle(ImVec2(cx + sep + 1, cy + 1), r, shadow, 0, 3.0f);
            dl->AddCircle(ImVec2(cx - sep, cy), r, col, 0, 3.0f);
            dl->AddCircle(ImVec2(cx + sep, cy), r, col, 0, 3.0f);
        } else {
            char ammo_text[32];
            snprintf(ammo_text, sizeof(ammo_text), "%d / %d", w.ammo, w.magazine.capacity);
            draw_text_shadowed(dl, font_large, 42.0f, ImVec2(ax, ay + 24.0f),
                               IM_COL32(255, 255, 255, 255), ammo_text);
        }

        // Reload progress bar
        if (w.state == WeaponState::RELOADING) {
            float reload_frac = 1.0f - w.reload_timer / w.config.reload_time;
            if (reload_frac < 0.0f) reload_frac = 0.0f;
            if (reload_frac > 1.0f) reload_frac = 1.0f;

            float rb_w = 200.0f;
            float rb_h = 6.0f;
            float rb_x = ax;
            float rb_y = ay - 14.0f;

            dl->AddRectFilled(ImVec2(rb_x, rb_y), ImVec2(rb_x + rb_w, rb_y + rb_h),
                              IM_COL32(0, 0, 0, 140), 3.0f);
            dl->AddRectFilled(ImVec2(rb_x, rb_y), ImVec2(rb_x + rb_w * reload_frac, rb_y + rb_h),
                              IM_COL32(255, 220, 60, 220), 3.0f);

            draw_text_shadowed(dl, font, 21.0f, ImVec2(rb_x + rb_w + 8, rb_y - 8),
                               IM_COL32(255, 220, 60, 220), "RELOADING");
        }
    }

    // ---- Gold + Room (top right) ----
    {
        char gold_text[32];
        snprintf(gold_text, sizeof(gold_text), "%d G", gs.currency);
        ImVec2 gt_sz = font->CalcTextSizeA(21.0f, FLT_MAX, 0.0f, gold_text);
        float gx = sw - gt_sz.x - 20.0f;
        draw_text_shadowed(dl, font, 21.0f, ImVec2(gx, 22.0f),
                           IM_COL32(255, 215, 50, 240), gold_text);

        char room_text[32];
        snprintf(room_text, sizeof(room_text), "ROOM %d", gs.rooms_cleared + 1);
        ImVec2 rt_sz = font->CalcTextSizeA(21.0f, FLT_MAX, 0.0f, room_text);
        float rx = sw - rt_sz.x - 20.0f;
        draw_text_shadowed(dl, font, 21.0f, ImVec2(rx, 46.0f),
                           IM_COL32(200, 200, 210, 200), room_text);
    }



    // ---- Door prompts (bottom center) ----
    if (!gs.in_shop_room) {
        for (const auto& d : gs.active_doors) {
            if (!d.is_exit) continue;
            const char* prompt = nullptr;
            ImU32 col = IM_COL32(255, 255, 255, 200);
            char buf[64];

            if (d.locked) {
                snprintf(buf, sizeof(buf), "LOCKED - %d REMAINING", ctx.enemy_count);
                prompt = buf;
                col = IM_COL32(255, 80, 80, 220);
            } else if (ctx.near_exit_door) {
                snprintf(buf, sizeof(buf), "PRESS %s TO CONTINUE",
                         input_code_name(gs.kb.get(Action::Interact, 0)));
                prompt = buf;
                col = IM_COL32(255, 255, 100, 240);
            } else {
                prompt = "EXIT OPEN";
                col = IM_COL32(100, 255, 100, 180);
            }

            if (prompt) {
                ImVec2 psz = font->CalcTextSizeA(21.0f, FLT_MAX, 0.0f, prompt);
                float px = (sw - psz.x) * 0.5f;
                float py = sh - 140.0f;
                draw_text_shadowed(dl, font, 21.0f, ImVec2(px, py), col, prompt);
            }
            break; // only first exit door
        }
    }
}

// ============================================================
//  Debug HUD (ImGui window)
// ============================================================
void hud_draw_debug(GameState& gs, const HudContext& ctx) {
    if (!gs.show_hud || gs.show_settings) return;

    ImGui::SetNextWindowPos(ImVec2(10, 10));
    ImGui::SetNextWindowBgAlpha(0.4f);
    ImGui::Begin("##hud_debug", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);

    ImGui::Text("FPS: %.0f", ctx.display_fps);
    float speed_xz = sqrtf(gs.player.velocity.X * gs.player.velocity.X +
                            gs.player.velocity.Z * gs.player.velocity.Z);
    ImGui::Text("Speed: %.1f u/s", speed_xz);
    ImGui::Text("HP: %.0f / %.0f", gs.player.health, gs.player.max_health);
    ImGui::Text("Pos: %.1f %.1f %.1f", gs.player.position.X, gs.player.position.Y, gs.player.position.Z);

    const char* state = "AIR";
    if (gs.player.grounded) {
        if (gs.player.sliding)       state = gs.player.power_sliding ? "POWER SLIDE" : "SLIDE";
        else if (gs.player.crouched) state = "CROUCH";
        else                         state = "GROUND";
    }
    ImGui::Text("%s", state);
    if (gs.player.lurch_timer > 0.0f) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1,0.7f,0.2f,1), "LURCH");
    }
    if (gs.noclip) ImGui::Text("NOCLIP");

    if (ctx.enemy_count > 0)
        ImGui::Text("Enemies: %d", ctx.enemy_count);
    if (gs.rooms_cleared > 0)
        ImGui::Text("Room: %d  Diff: %.2f", gs.rooms_cleared + 1, gs.procgen_cfg.difficulty);
    ImGui::Text("Gold: %d", gs.currency);

    ImGui::Separator();
    {
        Weapon& w = gs.weapons[gs.active_weapon];
        ImGui::Text("%s  %d / %d", w.config.name, w.ammo, w.magazine.capacity);

        float rate = w.config.fire_rate * w.bonuses.fire_rate_mult;
        int next_round = w.config.infinite_ammo
            ? w.current_round
            : (w.magazine.capacity - w.ammo);
        if (next_round >= 0 && next_round < w.magazine.capacity) {
            RoundMod next_mod = w.magazine.get(next_round);
            if (next_mod.tipping == Tipping::Aerodynamic) rate *= 1.2f;
        }
        ImGui::TextColored(ImVec4(0.7f, 0.8f, 0.9f, 1.0f), "%.1f rds/s", rate);

        if (w.state == WeaponState::RELOADING) {
            float pct = 1.0f - w.reload_timer / w.config.reload_time;
            ImGui::ProgressBar(pct, ImVec2(-1, 4), "");
        }
    }

    ImGui::End();
}

// ============================================================
//  Overlay: damage vignette + crosshair
// ============================================================
void hud_draw_overlay(GameState& gs) {
    // --- Damage vignette ---
    {
        float intensity = gs.player.damage_accum / 40.0f;
        if (intensity > 1.0f) intensity = 1.0f;
        if (intensity > 0.01f) {
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            ImVec2 sz = ImGui::GetIO().DisplaySize;
            float alpha = intensity * 0.6f;
            float border = sz.x * 0.15f;
            ImU32 red_full = ImGui::ColorConvertFloat4ToU32(ImVec4(0.8f, 0.0f, 0.0f, alpha));
            ImU32 red_zero = ImGui::ColorConvertFloat4ToU32(ImVec4(0.8f, 0.0f, 0.0f, 0.0f));
            fg->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(sz.x, border),
                red_full, red_full, red_zero, red_zero);
            fg->AddRectFilledMultiColor(ImVec2(0, sz.y - border), ImVec2(sz.x, sz.y),
                red_zero, red_zero, red_full, red_full);
            fg->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(border, sz.y),
                red_full, red_zero, red_zero, red_full);
            fg->AddRectFilledMultiColor(ImVec2(sz.x - border, 0), ImVec2(sz.x, sz.y),
                red_zero, red_full, red_full, red_zero);
        }
    }

    // --- Crosshair ---
    {
        ImDrawList* draw = ImGui::GetForegroundDrawList();
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
        float cs = 8.0f;
        float ct = 2.0f;
        ImU32 cross_col = IM_COL32(255, 255, 255, 200);
        draw->AddLine(ImVec2(center.x - cs, center.y), ImVec2(center.x + cs, center.y), cross_col, ct);
        draw->AddLine(ImVec2(center.x, center.y - cs), ImVec2(center.x, center.y + cs), cross_col, ct);
    }
}
