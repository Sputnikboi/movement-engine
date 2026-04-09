#include "room_stats.h"
#include "vendor/imgui/imgui.h"
#include <cstdio>

static void stat_row(const char* label, float value, ImU32 color = IM_COL32(220, 215, 200, 255)) {
    if (value < 0.5f) return; // skip zero entries
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextColored(ImColor(color).Value, "  %s", label);
    ImGui::TableNextColumn();
    ImGui::TextColored(ImColor(color).Value, "%.0f", value);
}

static void gold_row(const char* label, int value, ImU32 color = IM_COL32(255, 220, 80, 255)) {
    if (value == 0) return;
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextColored(ImColor(color).Value, "  %s", label);
    ImGui::TableNextColumn();
    ImGui::TextColored(ImColor(color).Value, "%d", value);
}

bool draw_room_summary(const RoomStats& stats, int room_number) {
    bool dismissed = false;

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 center(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);

    // Darken background
    ImGui::GetForegroundDrawList()->AddRectFilled(
        ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 140));

    float win_w = 420.0f;
    float win_h = 500.0f;
    ImGui::SetNextWindowPos(ImVec2(center.x - win_w * 0.5f, center.y - win_h * 0.5f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(win_w, 0), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.92f);

    ImGui::Begin("##room_summary", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoNav);

    // Title
    {
        char title[64];
        snprintf(title, sizeof(title), "Room %d Cleared!", room_number);
        ImVec2 tsz = ImGui::CalcTextSize(title);
        ImGui::SetCursorPosX((win_w - tsz.x) * 0.5f);
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s", title);
    }
    ImGui::Separator();
    ImGui::Spacing();

    // --- Damage Dealt ---
    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "Damage Dealt");
    if (ImGui::BeginTable("##dmg_dealt", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Amount", ImGuiTableColumnFlags_WidthFixed, 80.0f);

        stat_row("Base damage", stats.dmg_base, IM_COL32(200, 200, 200, 255));
        stat_row("Sharpened", stats.dmg_sharpened, IM_COL32(200, 80, 80, 255));
        stat_row("Crystal Tipped", stats.dmg_crystal, IM_COL32(140, 200, 240, 255));
        stat_row("Aerodynamic", stats.dmg_aerodynamic, IM_COL32(100, 220, 180, 255));
        stat_row("Poison", stats.dmg_poison, IM_COL32(80, 200, 80, 255));
        stat_row("Bleed bonus", stats.dmg_bleed_bonus, IM_COL32(200, 60, 60, 255));

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "  Total");
        ImGui::TableNextColumn();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%.0f", stats.dmg_total);

        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // --- Damage Taken ---
    ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "Damage Taken");
    if (stats.taken_total <= 0.0f) {
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "  No damage taken!  +5 gold");
    } else {
        if (ImGui::BeginTable("##dmg_taken", 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Amount", ImGuiTableColumnFlags_WidthFixed, 80.0f);

            stat_row("Rusher",      stats.taken_rusher,     IM_COL32(200, 200, 200, 255));
            stat_row("Turret",      stats.taken_turret,     IM_COL32(200, 200, 200, 255));
            stat_row("Tank",        stats.taken_tank,       IM_COL32(200, 200, 200, 255));
            stat_row("Bomber",      stats.taken_bomber,     IM_COL32(200, 200, 200, 255));
            stat_row("Projectile",  stats.taken_projectile, IM_COL32(200, 200, 200, 255));

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f), "  Total");
            ImGui::TableNextColumn();
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f), "%.0f", stats.taken_total);

            ImGui::EndTable();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // --- Gold Earned ---
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "Gold Earned");
    if (ImGui::BeginTable("##gold", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Amount", ImGuiTableColumnFlags_WidthFixed, 80.0f);

        gold_row("Drones",   stats.gold_drone);
        gold_row("Rushers",  stats.gold_rusher);
        gold_row("Turrets",  stats.gold_turret);
        gold_row("Tanks",    stats.gold_tank);
        gold_row("Bombers",  stats.gold_bomber);
        gold_row("Shielders",stats.gold_shielder);
        if (stats.gold_no_damage > 0)
            gold_row("No damage bonus", stats.gold_no_damage, IM_COL32(100, 255, 140, 255));

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.3f, 1.0f), "  Total");
        ImGui::TableNextColumn();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.3f, 1.0f), "%d", stats.gold_total);

        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Kills
    {
        char kills_str[32];
        snprintf(kills_str, sizeof(kills_str), "%d kills", stats.kills_total);
        ImVec2 ksz = ImGui::CalcTextSize(kills_str);
        ImGui::SetCursorPosX((win_w - ksz.x) * 0.5f);
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "%s", kills_str);
    }

    ImGui::Spacing();

    // Continue button
    {
        const char* btn = "Continue";
        ImVec2 bsz = ImGui::CalcTextSize(btn);
        float bw = bsz.x + 40.0f;
        ImGui::SetCursorPosX((win_w - bw) * 0.5f);
        if (ImGui::Button(btn, ImVec2(bw, 0))) {
            dismissed = true;
        }
    }

    // Also allow Space / Enter / E to dismiss
    if (ImGui::IsKeyPressed(ImGuiKey_Space) ||
        ImGui::IsKeyPressed(ImGuiKey_Enter) ||
        ImGui::IsKeyPressed(ImGuiKey_E)) {
        dismissed = true;
    }

    ImGui::End();

    return dismissed;
}
