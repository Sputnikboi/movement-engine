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
        } else if (s.type == ShopStandType::Healthpack) {
            s.cost = 5;
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
static void spawn_enemies_from_level(GameState& gs, const LevelData& pld) {
    for (const auto& es : pld.enemy_spawns) {
        if (es.type == EntityType::Drone)
            drone_spawn(gs.entities, gs.max_entities, es.position, gs.drone_cfg);
        else if (es.type == EntityType::Rusher)
            rusher_spawn(gs.entities, gs.max_entities, es.position, gs.rusher_cfg);
        else if (es.type == EntityType::Turret)
            turret_spawn(gs.entities, gs.max_entities, es.position, gs.turret_cfg);
        else if (es.type == EntityType::Tank)
            tank_spawn(gs.entities, gs.max_entities, es.position, gs.tank_cfg);
        else if (es.type == EntityType::Bomber)
            bomber_spawn(gs.entities, gs.max_entities, es.position, gs.bomber_cfg);
        else if (es.type == EntityType::Shielder)
            shielder_spawn(gs.entities, gs.max_entities, es.position, gs.shielder_cfg);
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
                    if (lvl > 0) {
                        // Upgrade existing weapon
                        gs.currency -= s.cost;
                        gs.weapon_level[w]++;
                        switch (w) {
                            case 0: gs.weapons[w].init_glock();   break;
                            case 1: gs.weapons[w].init_wingman(); break;
                            case 2: gs.weapons[w].init_knife();   break;
                        }
                        gs.apply_weapon_upgrades(w);
                        s.purchased = true;
                        printf("Upgraded %s to Lv %d\n", s.label, gs.weapon_level[w]);
                    } else {
                        // Buy new weapon (replace current)
                        gs.currency -= s.cost;
                        gs.weapon_level[gs.active_weapon] = 0;
                        gs.weapon_level[w] = 1;
                        gs.active_weapon = w;
                        gs.weapons[w].ammo = gs.weapons[w].config.mag_size;
                        gs.weapons[w].state = WeaponState::IDLE;
                        gs.num_weapons = 1;
                        s.purchased = true;
                        printf("Bought %s\n", s.label);
                    }
                    gs.shop_interact_cooldown = 0.3f;
                }
            } else if (s.type == ShopStandType::Healthpack) {
                bool full_hp = (gs.player.health >= gs.player.max_health - 0.1f);
                if (!full_hp && gs.currency >= s.cost) {
                    gs.currency -= s.cost;
                    gs.player.health += gs.player.max_health * 0.25f;
                    if (gs.player.health > gs.player.max_health)
                        gs.player.health = gs.player.max_health;
                    s.purchased = true;
                    gs.shop_interact_cooldown = 0.3f;
                    printf("Bought healthpack\n");
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

        gs.rooms_cleared++;
        gs.player.health = gs.player.max_health;
        gs.player.damage_accum = 0.0f;
        for (int w = 0; w < GameState::MAX_WEAPONS; w++) {
            gs.weapons[w].ammo = gs.weapons[w].config.mag_size;
            gs.weapons[w].state = WeaponState::IDLE;
            gs.weapons[w].reload_phase = ReloadPhase::NONE;
        }
        printf("Entering room %d...\n", gs.rooms_cleared + 1);
        gs.procgen_cfg.seed = 0;
        gs.procgen_cfg.room_number = gs.rooms_cleared + 1;
        gs.procgen_cfg.difficulty = 1.0f + gs.rooms_cleared * 0.15f;

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
                float tilt_deg = (w == 2) ? 75.0f : -75.0f;
                HMM_Mat4 tilt = HMM_Rotate_RH(HMM_AngleDeg(tilt_deg), HMM_V3(1, 0, 0));
                HMM_Mat4 spin = HMM_Rotate_RH(spin_yaw, HMM_V3(0, 1, 0));

                // Order: model_fix -> tilt -> spin
                HMM_Mat4 rot = HMM_MulM4(spin, HMM_MulM4(tilt, model_fix));

                append_mesh_transformed(out, gs.weapons[w].viewmodel_mesh,
                                        display_pos, rot, display_scale);
            }
        }
        // Healthpack cross stays as static geometry (built into room mesh)
    }
}

// ============================================================
//  Shop room HUD
// ============================================================

void shop_draw_hud(GameState& gs) {
    if (!gs.in_shop_room || !gs.show_hud || gs.show_settings) return;

    // Gold display at top center
    {
        char gold_buf[64];
        snprintf(gold_buf, sizeof(gold_buf), "Gold: %d", gs.currency);
        ImVec2 text_sz = ImGui::CalcTextSize(gold_buf);
        float cx = gs.renderer.swapchain_width() * 0.5f - text_sz.x * 0.5f;
        ImGui::SetNextWindowPos(ImVec2(cx, 40));
        ImGui::SetNextWindowBgAlpha(0.5f);
        ImGui::Begin("##shop_gold", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "%s", gold_buf);
        ImGui::End();
    }

    // Stand interaction prompt (bottom center)
    if (gs.shop_nearby_stand >= 0) {
        const ShopStand& s = gs.shop_data.stands[gs.shop_nearby_stand];
        ImVec2 prompt_pos(gs.renderer.swapchain_width() * 0.5f,
                          gs.renderer.swapchain_height() * 0.7f);
        ImGui::SetNextWindowPos(prompt_pos, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowBgAlpha(0.6f);
        ImGui::Begin("##shop_prompt", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);

        if (s.type == ShopStandType::Empty) {
            ImGui::TextDisabled("Coming Soon");
        } else if (s.purchased) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "SOLD");
        } else {
            const char* interact_key = input_code_name(gs.kb.get(Action::Interact, 0));
            if (s.type == ShopStandType::Weapon) {
                int w = s.weapon_index;
                int lvl = gs.weapon_level[w];
                const char* wnames[] = {"Glock", "Wingman", "Throwing Knife"};
                const char* upgrade_desc[] = {
                    "+1 dmg, +5%% fire rate",
                    "1.1x damage",
                    "+5 dmg, +0.1x crit mult"
                };
                if (lvl > 0) {
                    ImGui::Text("%s  Lv %d -> %d", wnames[w], lvl, lvl + 1);
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", upgrade_desc[w]);
                } else {
                    ImGui::Text("%s", wnames[w]);
                }
                if (gs.currency >= s.cost)
                    ImGui::TextColored(ImVec4(1,1,0.3f,1), "[%s] %s  (%d gold)",
                                       interact_key, lvl > 0 ? "Upgrade" : "Buy", s.cost);
                else
                    ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "Not enough gold (%d)", s.cost);
            } else if (s.type == ShopStandType::Healthpack) {
                bool full_hp = (gs.player.health >= gs.player.max_health - 0.1f);
                ImGui::Text("Healthpack +25%%");
                if (full_hp)
                    ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1), "Full HP");
                else if (gs.currency >= s.cost)
                    ImGui::TextColored(ImVec4(1,1,0.3f,1), "[%s] Buy  (%d gold)",
                                       interact_key, s.cost);
                else
                    ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "Not enough gold (%d)", s.cost);
            }
        }
        ImGui::End();
    }

    // Exit door prompt
    {
        HMM_Vec3 diff = HMM_SubV3(gs.player.position, gs.shop_data.exit_door_pos);
        diff.Y = 0;
        if (HMM_DotV3(diff, diff) < 3.0f * 3.0f && gs.shop_nearby_stand < 0) {
            ImVec2 prompt_pos(gs.renderer.swapchain_width() * 0.5f,
                              gs.renderer.swapchain_height() * 0.7f);
            ImGui::SetNextWindowPos(prompt_pos, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowBgAlpha(0.6f);
            ImGui::Begin("##shop_exit_prompt", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
            ImGui::TextColored(ImVec4(0.3f,1,0.3f,1), "Press [%s] to continue to next room",
                               input_code_name(gs.kb.get(Action::Interact, 0)));
            ImGui::End();
        }
    }
}
