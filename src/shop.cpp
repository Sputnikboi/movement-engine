#include "shop.h"
#include "game_state.h"
#include "procgen.h"
#include "drone.h"
#include "rusher.h"
#include "turret.h"
#include "tank.h"
#include "bomber.h"
#include "shielder.h"

#include "vendor/imgui/imgui.h"

#include <cstdio>
#include <cstdlib>

// ============================================================
//  Enter shop room (from combat room exit door)
// ============================================================

void shop_enter(GameState& gs) {
    gs.in_shop_room = true;
    gs.show_shop = true;  // gates entity updates / weapon switching

    // Full heal on entering shop
    gs.player.health = gs.player.max_health;

    // Pick weapon to offer this shop visit (can be active weapon for upgrade)
    if (gs.shop_weapon < 0) {
        gs.shop_weapon = rand() % 3;
    }

    // Generate the shop room
    gs.shop_data = generate_shop_room(gs.door_mesh_ptr, &gs.active_doors);

    // Configure stands
    const char* wnames[] = {"Glock", "Wingman", "Throwing Knife"};
    for (auto& s : gs.shop_data.stands) {
        s.purchased = false;
        if (s.type == ShopStandType::Weapon) {
            s.weapon_index = gs.shop_weapon;
            s.label = wnames[gs.shop_weapon];
            s.cost = 10;
        } else if (s.type == ShopStandType::Reroll) {
            s.reroll_cost = 10;
            s.cost = s.reroll_cost;
            s.label = "Reroll";
        } else if (s.type == ShopStandType::ShopItem) {
            // Roll from item pool: 50/50 tipping or enchantment
            if (rand() % 2 == 0) {
                s.type = ShopStandType::ModTipping;
                int t = 1 + rand() % ((int)Tipping::COUNT - 1);
                s.offered_tipping = static_cast<Tipping>(t);
                s.label = tipping_name(s.offered_tipping);
                s.cost = 8;
            } else {
                s.type = ShopStandType::ModEnchantment;
                int e = 1 + rand() % ((int)Enchantment::COUNT - 1);
                s.offered_enchantment = static_cast<Enchantment>(e);
                s.label = enchantment_name(s.offered_enchantment);
                s.cost = 8;
            }
        } else if (s.type == ShopStandType::ModTipping) {
            int t = 1 + rand() % ((int)Tipping::COUNT - 1);
            s.offered_tipping = static_cast<Tipping>(t);
            s.label = tipping_name(s.offered_tipping);
            s.cost = 8;
        } else if (s.type == ShopStandType::ModEnchantment) {
            int e = 1 + rand() % ((int)Enchantment::COUNT - 1);
            s.offered_enchantment = static_cast<Enchantment>(e);
            s.label = enchantment_name(s.offered_enchantment);
            s.cost = 8;
        }
    }

    // Clear entities + effects
    for (int i = 0; i < gs.max_entities; i++) gs.entities[i].alive = false;
    gs.effects.init();

    // Build collision from shop room mesh
    gs.collision.triangles.clear();
    gs.collision.ladder_volumes.clear();
    gs.collision.build_from_mesh(gs.shop_data.level.mesh);

    // Upload to renderer
    gs.renderer.reload_mesh(gs.shop_data.level.mesh);

    // Spawn player at shop entrance
    gs.player.position = gs.shop_data.level.spawn_pos;
    gs.player.velocity = HMM_V3(0, 0, 0);
    gs.camera.yaw = HMM_PI32 / 2.0f;  // face +Z (into shop)
    gs.camera.pitch = 0;
    gs.noclip = false;

    gs.shop_interact_cooldown = 0.3f; // prevent instant-buy on entry

    gs.current_level_name = "Shop";
    printf("Entering shop room...\n");
}

// ============================================================
//  Shop tick — stand interaction + exit
// ============================================================

// Spawn enemies from level data with difficulty scaling
void spawn_enemies_from_level(GameState& gs, const LevelData& pld) {
    float diff = gs.procgen_cfg.difficulty;
    float hp_s = 1.0f + (diff - 1.0f) * gs.procgen_cfg.hp_scale;
    float dm_s = 1.0f + (diff - 1.0f) * gs.procgen_cfg.dmg_scale;
    float sp_s = 1.0f + (diff - 1.0f) * gs.procgen_cfg.spd_scale;

    printf("Enemy scaling: HP=%.1fx DMG=%.1fx SPD=%.1fx (diff=%.2f)\n", hp_s, dm_s, sp_s, diff);

    // Scale from defaults each time (don't accumulate)
    DroneConfig    dr;  dr.drone_health *= hp_s; dr.projectile_damage *= dm_s; dr.chase_speed_min *= sp_s; dr.chase_speed_max *= sp_s; dr.circle_speed_min *= sp_s; dr.circle_speed_max *= sp_s;
    RusherConfig   ru;  ru.health *= hp_s; ru.melee_damage *= dm_s; ru.chase_speed *= sp_s; ru.dash_force *= sp_s;
    TurretConfig   tu;  tu.health *= hp_s; tu.beam_dps *= dm_s; tu.track_speed *= sp_s;
    TankConfig     tk;  tk.health *= hp_s; tk.stomp_damage *= dm_s; tk.chase_speed *= sp_s;
    BomberConfig   bo;  bo.health *= hp_s; bo.explosion_damage *= dm_s; bo.approach_speed *= sp_s; bo.dive_speed *= sp_s;
    ShielderConfig sh;  sh.health *= hp_s; sh.chase_speed *= sp_s; sh.flee_speed *= sp_s;

    // Write scaled configs back so update/damage functions use them
    gs.drone_cfg = dr;
    gs.rusher_cfg = ru;
    gs.turret_cfg = tu;
    gs.tank_cfg = tk;
    gs.bomber_cfg = bo;
    gs.shielder_cfg = sh;

    for (const auto& es : pld.enemy_spawns) {
        if (es.type == EntityType::Drone)
            drone_spawn(gs.entities, gs.max_entities, es.position, dr);
        else if (es.type == EntityType::Rusher)
            rusher_spawn(gs.entities, gs.max_entities, es.position, ru);
        else if (es.type == EntityType::Turret)
            turret_spawn(gs.entities, gs.max_entities, es.position, tu);
        else if (es.type == EntityType::Tank)
            tank_spawn(gs.entities, gs.max_entities, es.position, tk);
        else if (es.type == EntityType::Bomber)
            bomber_spawn(gs.entities, gs.max_entities, es.position, bo);
        else if (es.type == EntityType::Shielder)
            shielder_spawn(gs.entities, gs.max_entities, es.position, sh);
    }
}

bool shop_tick(GameState& gs, float dt, bool interact_pressed) {
    if (!gs.in_shop_room) return false;

    gs.shop_interact_cooldown -= dt;
    if (gs.shop_interact_cooldown < 0) gs.shop_interact_cooldown = 0;

    // Find nearest stand the player is close to
    gs.shop_nearby_stand = -1;
    float best_dist_sq = 2.2f * 2.2f;  // interaction radius
    for (int i = 0; i < (int)gs.shop_data.stands.size(); i++) {
        const auto& s = gs.shop_data.stands[i];
        HMM_Vec3 diff = HMM_SubV3(gs.player.position, s.position);
        diff.Y = 0;
        float d2 = HMM_DotV3(diff, diff);
        if (d2 < best_dist_sq) {
            best_dist_sq = d2;
            gs.shop_nearby_stand = i;
        }
    }

    // Check proximity to exit door
    bool near_shop_exit = false;
    {
        HMM_Vec3 diff = HMM_SubV3(gs.player.position, gs.shop_data.exit_door_pos);
        diff.Y = 0;
        if (HMM_DotV3(diff, diff) < 3.0f * 3.0f)
            near_shop_exit = true;
    }

    if (!interact_pressed || gs.shop_interact_cooldown > 0)
        return false;

    // Buy from stand
    if (gs.shop_nearby_stand >= 0) {
        ShopStand& s = gs.shop_data.stands[gs.shop_nearby_stand];
        if (!s.purchased && s.type != ShopStandType::Empty) {
            if (s.type == ShopStandType::Weapon) {
                int w = s.weapon_index;
                int lvl = gs.weapon_level[w];
                if (gs.currency >= s.cost) {
                    if (w == gs.active_weapon) {
                        // Upgrade current weapon — preserve magazine mods
                        gs.currency -= s.cost;
                        gs.weapon_level[w]++;
                        Magazine saved_mag = gs.weapons[w].magazine;
                        switch (w) {
                            case 0: gs.weapons[w].init_glock();   break;
                            case 1: gs.weapons[w].init_wingman(); break;
                            case 2: gs.weapons[w].init_knife();   break;
                        }
                        gs.weapons[w].magazine = saved_mag;
                        gs.apply_weapon_upgrades(w);
                        gs.weapons[w].recompute_bonuses();
                        // Recalc max HP from new bonuses
                        float new_max = 100.0f + gs.weapons[w].bonuses.bonus_max_hp;
                        if (new_max > gs.player.max_health)
                            gs.player.health += (new_max - gs.player.max_health);
                        gs.player.max_health = new_max;
                        if (gs.player.health > gs.player.max_health)
                            gs.player.health = gs.player.max_health;
                        s.purchased = true;
                        printf("Upgraded %s to Lv %d\n", s.label, gs.weapon_level[w]);
                    } else {
                        // Swap to different weapon (new or previously owned)
                        // Old weapon keeps its level and magazine
                        gs.currency -= s.cost;
                        int old = gs.active_weapon;
                        bool previously_owned = (gs.weapon_level[w] >= 1);
                        if (!previously_owned) gs.weapon_level[w] = 1;
                        Magazine saved_mag = gs.weapons[w].magazine;
                        gs.active_weapon = w;
                        switch (w) {
                            case 0: gs.weapons[w].init_glock();   break;
                            case 1: gs.weapons[w].init_wingman(); break;
                            case 2: gs.weapons[w].init_knife();   break;
                        }
                        if (previously_owned) gs.weapons[w].magazine = saved_mag;
                        gs.apply_weapon_upgrades(w);
                        gs.weapons[w].recompute_bonuses();
                        // Recalc max HP from new weapon's bonuses
                        float new_max = 100.0f + gs.weapons[w].bonuses.bonus_max_hp;
                        gs.player.max_health = new_max;
                        if (gs.player.health > gs.player.max_health)
                            gs.player.health = gs.player.max_health;
                        gs.weapons[w].state = WeaponState::IDLE;
                        s.purchased = true;
                        printf("Swapped %s for %s (Lv %d)\n",
                               gs.weapons[old].config.name, s.label, gs.weapon_level[w]);
                    }
                    gs.shop_interact_cooldown = 0.3f;
                }
            } else if (s.type == ShopStandType::Reroll) {
                if (gs.currency >= s.reroll_cost) {
                    gs.currency -= s.reroll_cost;
                    s.reroll_cost += 5;
                    s.cost = s.reroll_cost;
                    gs.shop_interact_cooldown = 0.3f;
                    /* Reroll weapon pedestal + all item stands */
                    const char* wnames_rr[] = {"Glock", "Wingman", "Throwing Knife"};
                    for (auto& rs : gs.shop_data.stands) {
                        if (rs.type == ShopStandType::Weapon && !rs.purchased) {
                            gs.shop_weapon = rand() % 3;
                            rs.weapon_index = gs.shop_weapon;
                            rs.label = wnames_rr[gs.shop_weapon];
                        }
                        if (rs.type == ShopStandType::ModTipping || rs.type == ShopStandType::ModEnchantment) {
                            rs.purchased = false;
                            if (rand() % 2 == 0) {
                                rs.type = ShopStandType::ModTipping;
                                int t = 1 + rand() % ((int)Tipping::COUNT - 1);
                                rs.offered_tipping = static_cast<Tipping>(t);
                                rs.label = tipping_name(rs.offered_tipping);
                                rs.cost = 8;
                            } else {
                                rs.type = ShopStandType::ModEnchantment;
                                int e = 1 + rand() % ((int)Enchantment::COUNT - 1);
                                rs.offered_enchantment = static_cast<Enchantment>(e);
                                rs.label = enchantment_name(rs.offered_enchantment);
                                rs.cost = 8;
                            }
                        }
                    }
                    printf("Rerolled shop (weapon: %s, next cost: %d)\n", wnames_rr[gs.shop_weapon], s.reroll_cost);
                }
            } else if (s.type == ShopStandType::ModTipping) {
                if (gs.currency >= s.cost) {
                    gs.currency -= s.cost;
                    gs.pending_mod = {};
                    gs.pending_mod.active = true;
                    gs.pending_mod.is_tipping = true;
                    gs.pending_mod.tipping = s.offered_tipping;
                    gs.pending_mod.max_applications = tipping_max_applications(s.offered_tipping);
                    gs.pending_mod.cost = s.cost;
                    gs.pending_mod.clear_selection();
                    gs.pending_stand_idx = gs.shop_nearby_stand;
                    gs.show_magazine_view = true;
                    SDL_SetWindowRelativeMouseMode(gs.window, false);
                    gs.shop_interact_cooldown = 0.3f;
                    printf("Selecting rounds for %s tipping (max %d)\n",
                           tipping_name(s.offered_tipping), gs.pending_mod.max_applications);
                }
            } else if (s.type == ShopStandType::ModEnchantment) {
                if (gs.currency >= s.cost) {
                    gs.currency -= s.cost;
                    gs.pending_mod = {};
                    gs.pending_mod.active = true;
                    gs.pending_mod.is_tipping = false;
                    gs.pending_mod.enchantment = s.offered_enchantment;
                    gs.pending_mod.max_applications = enchantment_max_applications(s.offered_enchantment);
                    gs.pending_mod.cost = s.cost;
                    gs.pending_mod.clear_selection();
                    gs.pending_stand_idx = gs.shop_nearby_stand;
                    gs.show_magazine_view = true;
                    SDL_SetWindowRelativeMouseMode(gs.window, false);
                    gs.shop_interact_cooldown = 0.3f;
                    printf("Selecting rounds for %s enchantment (max %d)\n",
                           enchantment_name(s.offered_enchantment), gs.pending_mod.max_applications);
                }
            }
        }
        return false;
    }

    // Exit shop → next combat room
    if (near_shop_exit) {
        gs.in_shop_room = false;
        gs.show_shop = false;
        gs.shop_weapon = -1;
        start_next_room(gs);
        return true;
    }

    return false;
}

// ============================================================
//  Shop display meshes (spinning weapon models on stands)
// ============================================================

static void append_mesh_transformed(Mesh& out, const Mesh& src,
                                    HMM_Vec3 pos, HMM_Mat4 rot, float scale) {
    uint32_t base = static_cast<uint32_t>(out.vertices.size());

    for (const auto& sv : src.vertices) {
        Vertex3D v = sv;
        HMM_Vec3 p = HMM_V3(sv.pos[0] * scale, sv.pos[1] * scale, sv.pos[2] * scale);
        HMM_Vec4 rp = HMM_MulM4V4(rot, HMM_V4(p.X, p.Y, p.Z, 1.0f));
        v.pos[0] = rp.X + pos.X;
        v.pos[1] = rp.Y + pos.Y;
        v.pos[2] = rp.Z + pos.Z;
        HMM_Vec4 rn = HMM_MulM4V4(rot, HMM_V4(sv.normal[0], sv.normal[1], sv.normal[2], 0.0f));
        v.normal[0] = rn.X;
        v.normal[1] = rn.Y;
        v.normal[2] = rn.Z;
        out.vertices.push_back(v);
    }
    for (uint32_t idx : src.indices)
        out.indices.push_back(base + idx);
}


// Build an octahedron (diamond) mesh at position with given half-size and color
static void append_diamond(Mesh& out, HMM_Vec3 pos, float rx, float ry, HMM_Vec3 color) {
    uint32_t base = (uint32_t)out.vertices.size();

    // 6 vertices: +Y, -Y, +X, -X, +Z, -Z
    HMM_Vec3 verts[6] = {
        {pos.X,      pos.Y + ry, pos.Z},       // 0 top
        {pos.X,      pos.Y - ry, pos.Z},       // 1 bottom
        {pos.X + rx, pos.Y,      pos.Z},       // 2 +X
        {pos.X - rx, pos.Y,      pos.Z},       // 3 -X
        {pos.X,      pos.Y,      pos.Z + rx},  // 4 +Z
        {pos.X,      pos.Y,      pos.Z - rx},  // 5 -Z
    };

    // 8 triangles (top 4, bottom 4)
    int tris[8][3] = {
        {0,2,4}, {0,4,3}, {0,3,5}, {0,5,2},  // top
        {1,4,2}, {1,3,4}, {1,5,3}, {1,2,5},  // bottom
    };

    for (int t = 0; t < 8; t++) {
        HMM_Vec3 a = verts[tris[t][0]];
        HMM_Vec3 b = verts[tris[t][1]];
        HMM_Vec3 c2 = verts[tris[t][2]];
        HMM_Vec3 edge1 = HMM_SubV3(b, a);
        HMM_Vec3 edge2 = HMM_SubV3(c2, a);
        HMM_Vec3 n = HMM_NormV3(HMM_Cross(edge1, edge2));

        for (int v = 0; v < 3; v++) {
            Vertex3D vtx{};
            vtx.pos[0] = verts[tris[t][v]].X;
            vtx.pos[1] = verts[tris[t][v]].Y;
            vtx.pos[2] = verts[tris[t][v]].Z;
            vtx.normal[0] = n.X;
            vtx.normal[1] = n.Y;
            vtx.normal[2] = n.Z;
            vtx.color[0] = color.X;
            vtx.color[1] = color.Y;
            vtx.color[2] = color.Z;
            out.vertices.push_back(vtx);
        }
        out.indices.push_back(base + t * 3 + 0);
        out.indices.push_back(base + t * 3 + 1);
        out.indices.push_back(base + t * 3 + 2);
    }
}

void shop_build_display_meshes(GameState& gs, Mesh& out, float time) {
    if (!gs.in_shop_room) return;

    float spin_yaw = time * 1.5f;  // ~1.5 rad/s, smooth spin

    for (const auto& s : gs.shop_data.stands) {
        if (s.purchased) continue;

        if (s.type == ShopStandType::Weapon) {
            int w = s.weapon_index;
            if (w >= 0 && w < GameState::MAX_WEAPONS && gs.weapons[w].mesh_loaded) {
                float display_scale = gs.weapons[w].config.model_scale * 2.0f;
                HMM_Vec3 display_pos = s.position;
                display_pos.Y += 0.4f;  // float above pedestal
                if (w == 2) display_pos.Y += 0.45f; // knife sits higher

                // Build rotation: model_rotation fix first, then tilt barrel/blade up, then spin
                HMM_Vec3 mr = gs.weapons[w].config.model_rotation;
                HMM_Mat4 model_fix = HMM_MulM4(
                    HMM_Rotate_RH(HMM_AngleDeg(mr.Z), HMM_V3(0, 0, 1)),
                    HMM_MulM4(
                        HMM_Rotate_RH(HMM_AngleDeg(mr.Y), HMM_V3(0, 1, 0)),
                        HMM_Rotate_RH(HMM_AngleDeg(mr.X), HMM_V3(1, 0, 0))
                    )
                );

                // After model_fix, barrel points along -Z. Tilt around X to point up.
                float tilt_deg = (w == 2) ? 75.0f : 75.0f;
                HMM_Mat4 tilt = HMM_Rotate_RH(HMM_AngleDeg(tilt_deg), HMM_V3(1, 0, 0));
                HMM_Mat4 spin = HMM_Rotate_RH(spin_yaw, HMM_V3(0, 1, 0));

                // Order: model_fix -> tilt -> spin
                HMM_Mat4 rot = HMM_MulM4(spin, HMM_MulM4(tilt, model_fix));

                append_mesh_transformed(out, gs.weapons[w].viewmodel_mesh,
                                        display_pos, rot, display_scale);
            }
        }
        // Reroll arrows stay as static geometry (built into room mesh)

        // Mod stand diamonds (tipping = orange, enchantment = purple)
        if (s.type == ShopStandType::ModTipping || s.type == ShopStandType::ModEnchantment) {
            HMM_Vec3 diamond_pos = s.position;
            diamond_pos.Y += 0.55f;  // float above pedestal
            HMM_Vec3 diamond_color;
            if (s.type == ShopStandType::ModTipping)
                diamond_color = HMM_V3(0.9f, 0.5f, 0.2f);   // orange
            else
                diamond_color = HMM_V3(0.5f, 0.27f, 0.9f);  // purple
            append_diamond(out, diamond_pos, 0.12f, 0.18f, diamond_color);
        }
    }
}

// ============================================================
//  Shop room HUD
// ============================================================

// Helper: draw text with shadow (same as hud.cpp)
static void shop_text_shadowed(ImDrawList* dl, ImFont* font, float size,
                                ImVec2 pos, ImU32 color, const char* text) {
    ImU32 shadow = IM_COL32(0, 0, 0, 180);
    dl->AddText(font, size, ImVec2(pos.x - 1, pos.y - 1), shadow, text);
    dl->AddText(font, size, ImVec2(pos.x + 1, pos.y - 1), shadow, text);
    dl->AddText(font, size, ImVec2(pos.x - 1, pos.y + 1), shadow, text);
    dl->AddText(font, size, ImVec2(pos.x + 1, pos.y + 1), shadow, text);
    dl->AddText(font, size, pos, color, text);
}

// Helper: draw centered text line, returns y advance. Updates max_w if provided.
static float shop_text_centered(ImDrawList* dl, ImFont* font, float size,
                                 float cx, float y, ImU32 color, const char* text,
                                 float* max_w = nullptr) {
    ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
    shop_text_shadowed(dl, font, size, ImVec2(cx - sz.x * 0.5f, y), color, text);
    if (max_w && sz.x > *max_w) *max_w = sz.x;
    return sz.y + 4.0f;
}

// Helper: world to screen projection
static bool shop_w2s(HMM_Vec3 wp, HMM_Mat4 vp, float sw, float sh, float& ox, float& oy) {
    HMM_Vec4 clip = HMM_MulM4V4(vp, HMM_V4(wp.X, wp.Y, wp.Z, 1.0f));
    if (clip.W <= 0.001f) return false;
    float inv = 1.0f / clip.W;
    ox = (clip.X * inv * 0.5f + 0.5f) * sw;
    oy = (clip.Y * inv * 0.5f + 0.5f) * sh;
    return true;
}

void shop_draw_hud(GameState& gs, ImFont* font, ImFont* font_large) {
    if (!gs.in_shop_room || gs.show_settings) return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImGuiIO& io = ImGui::GetIO();
    float sw = io.DisplaySize.x;
    float sh = io.DisplaySize.y;

    // Use default ImGui font as fallback
    if (!font) font = ImGui::GetFont();
    if (!font_large) font_large = font;

    float fs = 21.0f;  // small font size
    float fl = 42.0f;  // large font size

    // View-projection for world-to-screen
    float aspect = (sh > 0) ? sw / sh : 1.0f;
    HMM_Mat4 vp = HMM_MulM4(gs.camera.projection_matrix(aspect), gs.camera.view_matrix());



    // ---- Stand interaction popup (center screen) ----
    if (gs.shop_nearby_stand >= 0) {
        const ShopStand& s = gs.shop_data.stands[gs.shop_nearby_stand];
        float cx = sw * 0.5f;
        float base_y = sh * 0.6f;
        float y = base_y;
        float max_text_w = 0.0f; // track widest line for box sizing

        float box_pad = 16.0f;

        // Use channel splitter: ch0 = bg (behind), ch1 = text (front)
        ImDrawListSplitter splitter;
        splitter.Split(dl, 2);
        splitter.SetCurrentChannel(dl, 1); // draw text on front channel

        const char* interact_key = input_code_name(gs.kb.get(Action::Interact, 0));

        if (s.type == ShopStandType::Empty) {
            y += shop_text_centered(dl, font, fs, cx, y, IM_COL32(120, 120, 120, 200), "COMING SOON", &max_text_w);
        } else if (s.purchased) {
            y += shop_text_centered(dl, font, fs, cx, y, IM_COL32(120, 120, 120, 200), "SOLD", &max_text_w);
        } else if (s.type == ShopStandType::Weapon) {
            int w = s.weapon_index;
            int lvl = gs.weapon_level[w];
            const char* wnames[] = {"GLOCK", "WINGMAN", "THROWING KNIFE"};
            const char* upgrade_desc[] = {
                "+2 DMG  +15% RELOAD SPEED",
                "1.2X DAMAGE",
                "+5 DMG  +10% ATTACK SPEED"
            };

            // Title
            ImU32 title_col = IM_COL32(255, 220, 100, 255);
            if (w == gs.active_weapon) {
                char buf[64];
                snprintf(buf, sizeof(buf), "%s  LV %d > %d", wnames[w], lvl, lvl + 1);
                y += shop_text_centered(dl, font, fs, cx, y, title_col, buf, &max_text_w);
            } else {
                if (lvl > 0) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "%s  LV %d", wnames[w], lvl);
                    y += shop_text_centered(dl, font, fs, cx, y, title_col, buf, &max_text_w);
                } else {
                    y += shop_text_centered(dl, font, fs, cx, y, title_col, wnames[w], &max_text_w);
                }
            }

            // Desc
            if (w == gs.active_weapon) {
                y += shop_text_centered(dl, font, fs, cx, y, IM_COL32(180, 180, 180, 220), upgrade_desc[w], &max_text_w);
            } else {
                char buf[64];
                snprintf(buf, sizeof(buf), "REPLACES %s", gs.weapons[gs.active_weapon].config.name);
                // Uppercase the weapon name
                for (char* p = buf; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
                y += shop_text_centered(dl, font, fs, cx, y, IM_COL32(180, 180, 180, 220), buf, &max_text_w);
            }

            y += 4.0f;
            const char* action = (w == gs.active_weapon) ? "UPGRADE" : (lvl > 0 ? "SWAP" : "BUY");
            if (gs.currency >= s.cost) {
                char buf[64];
                snprintf(buf, sizeof(buf), "%s %s  %dG", interact_key, action, s.cost);
                y += shop_text_centered(dl, font, fs, cx, y, IM_COL32(255, 255, 80, 255), buf, &max_text_w);
            } else {
                char buf[64];
                snprintf(buf, sizeof(buf), "NOT ENOUGH GOLD  %dG", s.cost);
                y += shop_text_centered(dl, font, fs, cx, y, IM_COL32(255, 80, 80, 220), buf, &max_text_w);
            }
        } else if (s.type == ShopStandType::Reroll) {
            y += shop_text_centered(dl, font, fs, cx, y, IM_COL32(200, 200, 210, 255), "REROLL SHOP", &max_text_w);
            y += shop_text_centered(dl, font, fs, cx, y, IM_COL32(150, 150, 160, 200), "RANDOMIZE ALL ITEMS", &max_text_w);
            y += 4.0f;
            if (gs.currency >= s.reroll_cost) {
                char buf[64];
                snprintf(buf, sizeof(buf), "%s REROLL  %dG", interact_key, s.reroll_cost);
                y += shop_text_centered(dl, font, fs, cx, y, IM_COL32(255, 255, 80, 255), buf, &max_text_w);
            } else {
                char buf[64];
                snprintf(buf, sizeof(buf), "NOT ENOUGH GOLD  %dG", s.reroll_cost);
                y += shop_text_centered(dl, font, fs, cx, y, IM_COL32(255, 80, 80, 220), buf, &max_text_w);
            }
        } else if (s.type == ShopStandType::ModTipping) {
            ImU32 tip_col = IM_COL32(230, 130, 50, 255);
            char title[64];
            snprintf(title, sizeof(title), "TIPPING: %s", tipping_name(s.offered_tipping));
            for (char* p = title; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
            y += shop_text_centered(dl, font, fs, cx, y, tip_col, title, &max_text_w);
            // Description — uppercase it
            char desc[128];
            snprintf(desc, sizeof(desc), "%s", tipping_desc(s.offered_tipping));
            for (char* p = desc; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
            y += shop_text_centered(dl, font, fs, cx, y, IM_COL32(180, 180, 180, 220), desc, &max_text_w);
            int app = tipping_max_applications(s.offered_tipping);
            char abuf[32];
            snprintf(abuf, sizeof(abuf), "APPLIES TO %d ROUND%s", app, app == 1 ? "" : "S");
            y += shop_text_centered(dl, font, fs, cx, y, IM_COL32(160, 160, 170, 200), abuf, &max_text_w);
            y += 4.0f;
            if (gs.currency >= s.cost) {
                char buf[64];
                snprintf(buf, sizeof(buf), "%s BUY  %dG", interact_key, s.cost);
                y += shop_text_centered(dl, font, fs, cx, y, IM_COL32(255, 255, 80, 255), buf, &max_text_w);
            } else {
                char buf[64];
                snprintf(buf, sizeof(buf), "NOT ENOUGH GOLD  %dG", s.cost);
                y += shop_text_centered(dl, font, fs, cx, y, IM_COL32(255, 80, 80, 220), buf, &max_text_w);
            }
        } else if (s.type == ShopStandType::ModEnchantment) {
            ImU32 ench_col = IM_COL32(130, 70, 230, 255);
            char title[64];
            snprintf(title, sizeof(title), "ENCHANTMENT: %s", enchantment_name(s.offered_enchantment));
            for (char* p = title; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
            y += shop_text_centered(dl, font, fs, cx, y, ench_col, title, &max_text_w);
            char desc[128];
            snprintf(desc, sizeof(desc), "%s", enchantment_desc(s.offered_enchantment));
            for (char* p = desc; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
            y += shop_text_centered(dl, font, fs, cx, y, IM_COL32(180, 180, 180, 220), desc, &max_text_w);
            int eapp = enchantment_max_applications(s.offered_enchantment);
            char abuf[32];
            snprintf(abuf, sizeof(abuf), "APPLIES TO %d ROUND%s", eapp, eapp == 1 ? "" : "S");
            y += shop_text_centered(dl, font, fs, cx, y, IM_COL32(160, 160, 170, 200), abuf, &max_text_w);
            y += 4.0f;
            if (gs.currency >= s.cost) {
                char buf[64];
                snprintf(buf, sizeof(buf), "%s BUY  %dG", interact_key, s.cost);
                y += shop_text_centered(dl, font, fs, cx, y, IM_COL32(255, 255, 80, 255), buf, &max_text_w);
            } else {
                char buf[64];
                snprintf(buf, sizeof(buf), "NOT ENOUGH GOLD  %dG", s.cost);
                y += shop_text_centered(dl, font, fs, cx, y, IM_COL32(255, 80, 80, 220), buf, &max_text_w);
            }
        }

        // Draw background box on channel 0 (behind text)
        splitter.SetCurrentChannel(dl, 0);
        float box_h = y - base_y + box_pad * 2;
        float box_w = fmaxf(max_text_w + box_pad * 2, 200.0f);
        dl->AddRectFilled(
            ImVec2(cx - box_w * 0.5f, base_y - box_pad),
            ImVec2(cx + box_w * 0.5f, base_y - box_pad + box_h),
            IM_COL32(30, 30, 35, 230), 8.0f);
        dl->AddRect(
            ImVec2(cx - box_w * 0.5f, base_y - box_pad),
            ImVec2(cx + box_w * 0.5f, base_y - box_pad + box_h),
            IM_COL32(120, 115, 130, 100), 8.0f, 0, 1.5f);
        splitter.Merge(dl);
    }

    // ---- Exit door prompt ----
    {
        HMM_Vec3 diff = HMM_SubV3(gs.player.position, gs.shop_data.exit_door_pos);
        diff.Y = 0;
        if (HMM_DotV3(diff, diff) < 3.0f * 3.0f && gs.shop_nearby_stand < 0) {
            char buf[64];
            snprintf(buf, sizeof(buf), "PRESS %s TO CONTINUE",
                     input_code_name(gs.kb.get(Action::Interact, 0)));
            shop_text_centered(dl, font, fs, sw * 0.5f, sh * 0.7f,
                               IM_COL32(100, 255, 100, 240), buf);
        }
    }
}

// ============================================================
//  Start next combat room (used both from shop exit and direct skip)
// ============================================================
void start_next_room(GameState& gs) {
    gs.rooms_cleared++;
    gs.room_stats.reset();
    gs.player.damage_accum = 0.0f;
    for (int w = 0; w < GameState::MAX_WEAPONS; w++) {
        gs.weapons[w].ammo = gs.weapons[w].config.mag_size;
        gs.weapons[w].state = WeaponState::IDLE;
        gs.weapons[w].reload_phase = ReloadPhase::NONE;
    }
    printf("Entering room %d...\n", gs.rooms_cleared + 1);
    gs.procgen_cfg.seed = 0;
    gs.procgen_cfg.room_number = gs.rooms_cleared + 1;
    // Difficulty scales faster every 10 rooms
    {
        int r = gs.rooms_cleared;
        int tier = r / 10;
        float base_rate = 0.10f;
        float accel = 0.067f;
        float rate = base_rate + tier * accel;
        gs.procgen_cfg.difficulty = 1.0f + r * rate;
    }

    LevelData pld = generate_level(gs.procgen_cfg, gs.door_mesh_ptr, &gs.active_doors);

    for (int i = 0; i < gs.max_entities; i++) gs.entities[i].alive = false;
    gs.effects.init();

    gs.collision.triangles.clear();
    gs.collision.ladder_volumes.clear();
    gs.collision.build_from_mesh(pld.mesh);
    gs.renderer.reload_mesh(pld.mesh);

    gs.player.position = pld.spawn_pos;
    gs.player.velocity = HMM_V3(0, 0, 0);
    gs.camera.yaw = HMM_PI32 / 2.0f;
    gs.camera.pitch = 0;
    gs.noclip = false;

    spawn_enemies_from_level(gs, pld);

    gs.current_level_name = "Procedural";
    printf("Room transition complete.\n");
}
