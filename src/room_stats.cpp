#include "room_stats.h"
#include "vendor/imgui/imgui.h"
#include <cstdio>
#include <cmath>

static void rs_text(ImDrawList* dl, ImFont* f, float sz, ImVec2 pos, ImU32 col, const char* text) {
    ImU32 shadow = IM_COL32(0, 0, 0, 180);
    dl->AddText(f, sz, ImVec2(pos.x - 1, pos.y - 1), shadow, text);
    dl->AddText(f, sz, ImVec2(pos.x + 1, pos.y + 1), shadow, text);
    dl->AddText(f, sz, pos, col, text);
}

static float rs_row(ImDrawList* dl, ImFont* f, float fs, float lx, float rx, float y,
                    const char* label, float value, ImU32 col) {
    if (value < 0.5f) return 0.0f;
    char vbuf[32]; snprintf(vbuf, sizeof(vbuf), "%.0f", value);
    rs_text(dl, f, fs, ImVec2(lx, y), col, label);
    ImVec2 vsz = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, vbuf);
    rs_text(dl, f, fs, ImVec2(rx - vsz.x, y), col, vbuf);
    return fs + 4.0f;
}

static float rs_gold_row(ImDrawList* dl, ImFont* f, float fs, float lx, float rx, float y,
                         const char* label, int value, ImU32 col) {
    if (value == 0) return 0.0f;
    char vbuf[32]; snprintf(vbuf, sizeof(vbuf), "%d", value);
    rs_text(dl, f, fs, ImVec2(lx, y), col, label);
    ImVec2 vsz = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, vbuf);
    rs_text(dl, f, fs, ImVec2(rx - vsz.x, y), col, vbuf);
    return fs + 4.0f;
}

// Measure only (no draw). Returns total height consumed by the summary content.
static float measure_summary(ImFont* font, float fs, float box_w, float pad,
                             const RoomStats& stats, int room_number) {
    float y = pad; // start after top padding
    y += fs + 12.0f; // title
    y += 8.0f;       // separator
    y += fs + 6.0f;  // "DAMAGE DEALT"
    if (stats.dmg_base >= 0.5f)        y += fs + 4.0f;
    if (stats.dmg_sharpened >= 0.5f)   y += fs + 4.0f;
    if (stats.dmg_crystal >= 0.5f)     y += fs + 4.0f;
    if (stats.dmg_aerodynamic >= 0.5f) y += fs + 4.0f;
    if (stats.dmg_poison >= 0.5f)      y += fs + 4.0f;
    if (stats.dmg_bleed_bonus >= 0.5f) y += fs + 4.0f;
    y += 4.0f + fs + 10.0f; // total line
    y += 8.0f; // separator
    y += fs + 6.0f; // "DAMAGE TAKEN"
    if (stats.taken_total <= 0.0f) {
        y += fs + 6.0f;
    } else {
        if (stats.taken_rusher >= 0.5f)     y += fs + 4.0f;
        if (stats.taken_turret >= 0.5f)     y += fs + 4.0f;
        if (stats.taken_tank >= 0.5f)       y += fs + 4.0f;
        if (stats.taken_bomber >= 0.5f)     y += fs + 4.0f;
        if (stats.taken_projectile >= 0.5f) y += fs + 4.0f;
        y += 4.0f + fs + 10.0f;
    }
    y += 8.0f; // separator
    y += fs + 6.0f; // "GOLD EARNED"
    if (stats.gold_drone > 0)    y += fs + 4.0f;
    if (stats.gold_rusher > 0)   y += fs + 4.0f;
    if (stats.gold_turret > 0)   y += fs + 4.0f;
    if (stats.gold_tank > 0)     y += fs + 4.0f;
    if (stats.gold_bomber > 0)   y += fs + 4.0f;
    if (stats.gold_shielder > 0) y += fs + 4.0f;
    if (stats.gold_no_damage > 0) y += fs + 4.0f;
    if (stats.gold_gilded > 0)    y += fs + 4.0f;
    y += 4.0f + fs + 10.0f; // total
    y += 8.0f; // separator
    y += fs + 12.0f; // kills
    y += fs + 16.0f + 12.0f + pad; // continue button
    return y;
}

bool draw_room_summary(const RoomStats& stats, int room_number, ImFont* font) {
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    float sw = io.DisplaySize.x;
    float sh = io.DisplaySize.y;
    if (!font) font = ImGui::GetFont();
    float fs = 21.0f;

    // Full-screen darken
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(sw, sh), IM_COL32(0, 0, 0, 160));

    // Box
    float box_w = 440.0f;
    float pad = 16.0f;
    float box_h = measure_summary(font, fs, box_w, pad, stats, room_number);
    float box_x = (sw - box_w) * 0.5f;
    float box_top = (sh - box_h) * 0.5f; // vertically centered
    if (box_top < 20.0f) box_top = 20.0f;
    float lx = box_x + pad;
    float rx = box_x + box_w - pad;

    // Draw background FIRST
    dl->AddRectFilled(ImVec2(box_x, box_top), ImVec2(box_x + box_w, box_top + box_h),
                      IM_COL32(35, 35, 40, 245), 8.0f);
    dl->AddRect(ImVec2(box_x, box_top), ImVec2(box_x + box_w, box_top + box_h),
                IM_COL32(180, 170, 140, 120), 8.0f, 0, 1.5f);

    float y = box_top + pad;

    // Title
    {
        char title[64];
        snprintf(title, sizeof(title), "ROOM %d CLEARED", room_number);
        ImVec2 tsz = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, title);
        rs_text(dl, font, fs, ImVec2((sw - tsz.x) * 0.5f, y), IM_COL32(255, 220, 80, 255), title);
        y += fs + 12.0f;
    }

    dl->AddLine(ImVec2(lx, y), ImVec2(rx, y), IM_COL32(150, 150, 150, 120), 1.0f);
    y += 8.0f;

    // Damage Dealt
    rs_text(dl, font, fs, ImVec2(lx, y), IM_COL32(255, 130, 80, 255), "DAMAGE DEALT");
    y += fs + 6.0f;
    ImU32 sc = IM_COL32(200, 200, 200, 240);
    y += rs_row(dl, font, fs, lx + 10, rx, y, "BASE",           stats.dmg_base,        sc);
    y += rs_row(dl, font, fs, lx + 10, rx, y, "SHARPENED",      stats.dmg_sharpened,    IM_COL32(200, 80, 80, 255));
    y += rs_row(dl, font, fs, lx + 10, rx, y, "CRYSTAL TIPPED", stats.dmg_crystal,      IM_COL32(140, 200, 240, 255));
    y += rs_row(dl, font, fs, lx + 10, rx, y, "AERODYNAMIC",    stats.dmg_aerodynamic,  IM_COL32(100, 220, 180, 255));
    y += rs_row(dl, font, fs, lx + 10, rx, y, "POISON",         stats.dmg_poison,       IM_COL32(80, 200, 80, 255));
    y += rs_row(dl, font, fs, lx + 10, rx, y, "BLEED BONUS",    stats.dmg_bleed_bonus,  IM_COL32(200, 60, 60, 255));
    dl->AddLine(ImVec2(lx + 10, y), ImVec2(rx, y), IM_COL32(150, 150, 150, 80), 1.0f);
    y += 4.0f;
    {
        char tbuf[32]; snprintf(tbuf, sizeof(tbuf), "%.0f", stats.dmg_total);
        rs_text(dl, font, fs, ImVec2(lx + 10, y), IM_COL32(255, 200, 80, 255), "TOTAL");
        ImVec2 tsz = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, tbuf);
        rs_text(dl, font, fs, ImVec2(rx - tsz.x, y), IM_COL32(255, 200, 80, 255), tbuf);
        y += fs + 10.0f;
    }

    dl->AddLine(ImVec2(lx, y), ImVec2(rx, y), IM_COL32(150, 150, 150, 120), 1.0f);
    y += 8.0f;

    // Damage Taken
    rs_text(dl, font, fs, ImVec2(lx, y), IM_COL32(230, 80, 80, 255), "DAMAGE TAKEN");
    y += fs + 6.0f;
    if (stats.taken_total <= 0.0f) {
        rs_text(dl, font, fs, ImVec2(lx + 10, y), IM_COL32(80, 255, 130, 255), "NO DAMAGE  +5G");
        y += fs + 6.0f;
    } else {
        y += rs_row(dl, font, fs, lx + 10, rx, y, "RUSHER",     stats.taken_rusher,     sc);
        y += rs_row(dl, font, fs, lx + 10, rx, y, "TURRET",     stats.taken_turret,     sc);
        y += rs_row(dl, font, fs, lx + 10, rx, y, "TANK",       stats.taken_tank,       sc);
        y += rs_row(dl, font, fs, lx + 10, rx, y, "BOMBER",     stats.taken_bomber,     sc);
        y += rs_row(dl, font, fs, lx + 10, rx, y, "PROJECTILE", stats.taken_projectile, sc);
        dl->AddLine(ImVec2(lx + 10, y), ImVec2(rx, y), IM_COL32(150, 150, 150, 80), 1.0f);
        y += 4.0f;
        {
            char tbuf[32]; snprintf(tbuf, sizeof(tbuf), "%.0f", stats.taken_total);
            rs_text(dl, font, fs, ImVec2(lx + 10, y), IM_COL32(230, 100, 100, 255), "TOTAL");
            ImVec2 tsz = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, tbuf);
            rs_text(dl, font, fs, ImVec2(rx - tsz.x, y), IM_COL32(230, 100, 100, 255), tbuf);
            y += fs + 10.0f;
        }
    }

    dl->AddLine(ImVec2(lx, y), ImVec2(rx, y), IM_COL32(150, 150, 150, 120), 1.0f);
    y += 8.0f;

    // Gold Earned
    rs_text(dl, font, fs, ImVec2(lx, y), IM_COL32(255, 215, 50, 255), "GOLD EARNED");
    y += fs + 6.0f;
    ImU32 gc = IM_COL32(255, 220, 80, 255);
    y += rs_gold_row(dl, font, fs, lx + 10, rx, y, "DRONES",    stats.gold_drone,    gc);
    y += rs_gold_row(dl, font, fs, lx + 10, rx, y, "RUSHERS",   stats.gold_rusher,   gc);
    y += rs_gold_row(dl, font, fs, lx + 10, rx, y, "TURRETS",   stats.gold_turret,   gc);
    y += rs_gold_row(dl, font, fs, lx + 10, rx, y, "TANKS",     stats.gold_tank,     gc);
    y += rs_gold_row(dl, font, fs, lx + 10, rx, y, "BOMBERS",   stats.gold_bomber,   gc);
    y += rs_gold_row(dl, font, fs, lx + 10, rx, y, "SHIELDERS", stats.gold_shielder, gc);
    if (stats.gold_no_damage > 0)
        y += rs_gold_row(dl, font, fs, lx + 10, rx, y, "NO DAMAGE BONUS", stats.gold_no_damage, IM_COL32(100, 255, 140, 255));
    if (stats.gold_gilded > 0)
        y += rs_gold_row(dl, font, fs, lx + 10, rx, y, "GILDED BONUS", stats.gold_gilded, IM_COL32(255, 215, 80, 255));
    dl->AddLine(ImVec2(lx + 10, y), ImVec2(rx, y), IM_COL32(150, 150, 150, 80), 1.0f);
    y += 4.0f;
    {
        char tbuf[32]; snprintf(tbuf, sizeof(tbuf), "%d", stats.gold_total);
        rs_text(dl, font, fs, ImVec2(lx + 10, y), IM_COL32(255, 230, 60, 255), "TOTAL");
        ImVec2 tsz = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, tbuf);
        rs_text(dl, font, fs, ImVec2(rx - tsz.x, y), IM_COL32(255, 230, 60, 255), tbuf);
        y += fs + 10.0f;
    }

    dl->AddLine(ImVec2(lx, y), ImVec2(rx, y), IM_COL32(150, 150, 150, 120), 1.0f);
    y += 8.0f;

    // Kills
    {
        char kbuf[32]; snprintf(kbuf, sizeof(kbuf), "%d KILLS", stats.kills_total);
        ImVec2 ksz = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, kbuf);
        rs_text(dl, font, fs, ImVec2((sw - ksz.x) * 0.5f, y), IM_COL32(200, 200, 210, 220), kbuf);
        y += fs + 12.0f;
    }

    // Continue button
    bool dismissed = false;
    {
        const char* label = "CONTINUE";
        ImVec2 lsz = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, label);
        float btn_w = lsz.x + 40.0f;
        float btn_h = fs + 16.0f;
        float btn_x = (sw - btn_w) * 0.5f;
        float btn_y = y + 4.0f;

        ImVec2 mpos = ImGui::GetIO().MousePos;
        bool hovered = (mpos.x >= btn_x && mpos.x <= btn_x + btn_w &&
                        mpos.y >= btn_y && mpos.y <= btn_y + btn_h);

        ImU32 bg = hovered ? IM_COL32(80, 75, 60, 255) : IM_COL32(55, 52, 48, 255);
        ImU32 border = hovered ? IM_COL32(255, 230, 100, 255) : IM_COL32(180, 170, 130, 180);
        ImU32 text_col = hovered ? IM_COL32(255, 255, 120, 255) : IM_COL32(255, 255, 100, 220);

        dl->AddRectFilled(ImVec2(btn_x, btn_y), ImVec2(btn_x + btn_w, btn_y + btn_h), bg, 6.0f);
        dl->AddRect(ImVec2(btn_x, btn_y), ImVec2(btn_x + btn_w, btn_y + btn_h), border, 6.0f, 0, 1.5f);
        rs_text(dl, font, fs, ImVec2(btn_x + (btn_w - lsz.x) * 0.5f, btn_y + (btn_h - fs) * 0.5f), text_col, label);

        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            dismissed = true;

        y = btn_y + btn_h + 8.0f;
    }

    return dismissed;
}
