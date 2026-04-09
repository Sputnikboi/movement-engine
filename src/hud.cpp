#include "hud.h"
#include "game_state.h"

#include "vendor/imgui/imgui.h"

#include <cstdio>
#include <cmath>

void hud_draw(GameState& gs, const HudContext& ctx) {
    if (!gs.show_hud || gs.show_settings) return;

    // --- Main HUD panel (top-left) ---
    ImGui::SetNextWindowPos(ImVec2(10, 10));
    ImGui::SetNextWindowBgAlpha(0.4f);
    ImGui::Begin("##hud", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);

    ImGui::Text("FPS: %.0f", ctx.display_fps);
    float speed_xz = sqrtf(gs.player.velocity.X * gs.player.velocity.X +
                            gs.player.velocity.Z * gs.player.velocity.Z);
    ImGui::Text("Speed: %.1f u/s", speed_xz);

    // Health bar
    {
        float hp_frac = gs.player.health / gs.player.max_health;
        ImVec4 hp_color = (hp_frac > 0.5f)
            ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f)
            : (hp_frac > 0.25f)
                ? ImVec4(1.0f, 0.8f, 0.2f, 1.0f)
                : ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, hp_color);
        char hp_text[32];
        snprintf(hp_text, sizeof(hp_text), "HP: %.0f / %.0f", gs.player.health, gs.player.max_health);
        ImGui::ProgressBar(hp_frac, ImVec2(180, 14), hp_text);
        ImGui::PopStyleColor();
    }
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

    for (const auto& d : gs.active_doors) {
        if (d.is_exit) {
            if (!gs.in_shop_room) {
                if (d.locked)
                    ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "EXIT: LOCKED (%d remaining)", ctx.enemy_count);
                else if (ctx.near_exit_door)
                    ImGui::TextColored(ImVec4(1,1,0.3f,1), "Press [%s] to enter shop",
                                       input_code_name(gs.kb.get(Action::Interact, 0)));
                else
                    ImGui::TextColored(ImVec4(0.3f,1,0.3f,1), "EXIT: UNLOCKED");
            }
        }
    }

    // --- Weapon HUD ---
    ImGui::Separator();
    {
        Weapon& w = gs.weapons[gs.active_weapon];
        ImGui::Text("%s  %d / %d", w.config.name, w.ammo, w.config.mag_size);

        // Effective fire rate (accounts for next round's Aerodynamic tipping)
        {
            float rate = w.config.fire_rate * w.bonuses.fire_rate_mult;
            int next_round = w.config.infinite_ammo
                ? w.current_round
                : (w.magazine.capacity - w.ammo);
            if (next_round >= 0 && next_round < w.magazine.capacity) {
                RoundMod next_mod = w.magazine.get(next_round);
                if (next_mod.tipping == Tipping::Aerodynamic) rate *= 1.2f;
            }
            ImGui::TextColored(ImVec4(0.7f, 0.8f, 0.9f, 1.0f), "%.1f rds/s", rate);
        }

        if (w.state == WeaponState::RELOADING) {
            float pct = 1.0f - w.reload_timer / w.config.reload_time;
            ImGui::ProgressBar(pct, ImVec2(-1, 4), "");
            const char* phase_name =
                w.reload_phase == ReloadPhase::MAG_OUT  ? "MAG OUT" :
                w.reload_phase == ReloadPhase::MAG_SWAP ? "MAG SWAP" :
                w.reload_phase == ReloadPhase::GUN_UP   ? "GUN UP" : "RELOADING";
            ImGui::TextColored(ImVec4(1,1,0,1), "%s...", phase_name);
        }
        if (w.state == WeaponState::SWAPPING)
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Swapping...");
        if (w.ads_blend > 0.01f)
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "ADS");
    }

    ImGui::Separator();
    ImGui::TextDisabled("ESC: settings  H: hide HUD  R: reload  RMB: aim");

    ImGui::End();

    // --- Damage vignette ---
    {
        float intensity = gs.player.damage_accum / 40.0f; // 40 dmg = full intensity
        if (intensity > 1.0f) intensity = 1.0f;
        if (intensity > 0.01f) {
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            ImVec2 sz = ImGui::GetIO().DisplaySize;
            float alpha = intensity * 0.6f;
            float border = sz.x * 0.15f; // vignette width
            ImU32 red_full = ImGui::ColorConvertFloat4ToU32(ImVec4(0.8f, 0.0f, 0.0f, alpha));
            ImU32 red_zero = ImGui::ColorConvertFloat4ToU32(ImVec4(0.8f, 0.0f, 0.0f, 0.0f));
            // Top
            fg->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(sz.x, border),
                red_full, red_full, red_zero, red_zero);
            // Bottom
            fg->AddRectFilledMultiColor(ImVec2(0, sz.y - border), ImVec2(sz.x, sz.y),
                red_zero, red_zero, red_full, red_full);
            // Left
            fg->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(border, sz.y),
                red_full, red_zero, red_zero, red_full);
            // Right
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
