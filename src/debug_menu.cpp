#include "debug_menu.h"
#include "game_state.h"
#include "mesh.h"
#include "level_loader.h"
#include "procgen.h"
#include "drone.h"
#include "rusher.h"
#include "turret.h"
#include "tank.h"
#include "bomber.h"
#include "shielder.h"

#include "vendor/imgui/imgui.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

// ============================================================
//  Helpers
// ============================================================

static std::vector<std::string> scan_levels(const std::string& dir) {
    std::vector<std::string> files;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return files;
    for (auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension().string();
        for (auto& c : ext) c = static_cast<char>(tolower(c));
        if (ext == ".glb" || ext == ".gltf") {
            files.push_back(entry.path().string());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

// ============================================================
//  Debug / Settings Menu
// ============================================================

void debug_menu_draw(GameState& gs, const LoadLevelFn& load_level_fn) {
    if (!gs.show_settings) return;

    ImGui::SetNextWindowSize(ImVec2(440, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(420, 60), ImGuiCond_FirstUseEver);

    ImGui::Begin("Settings", &gs.show_settings);

    // ---- Level ----
    if (ImGui::CollapsingHeader("Level", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Current: %s", gs.current_level_name.c_str());
        ImGui::Spacing();

        // Scan for levels on first open
        if (!gs.levels_scanned) {
            gs.level_files = scan_levels("levels");
            if (gs.level_files.empty()) gs.level_files = scan_levels("../levels");
            if (gs.level_files.empty()) gs.level_files = scan_levels("../../levels");
            gs.levels_scanned = true;
        }

        if (ImGui::Button("Refresh Level List")) {
            gs.level_files = scan_levels("levels");
            if (gs.level_files.empty()) gs.level_files = scan_levels("../levels");
            if (gs.level_files.empty()) gs.level_files = scan_levels("../../levels");
        }

        ImGui::SameLine();
        if (ImGui::Button("Built-in Test Level")) {
            Mesh test = create_test_level();
            gs.renderer.reload_mesh(test);
            gs.collision.triangles.clear();
            gs.collision.build_from_mesh(test);
            gs.player.position = HMM_V3(0, 1, 15);
            gs.player.velocity = HMM_V3(0, 0, 0);
            gs.noclip = false;
            gs.camera.position = gs.player.eye_position();
            gs.current_level_name = "built-in test level";
        }

        if (!gs.level_files.empty()) {
            ImGui::Spacing();
            ImGui::BeginChild("##levellist", ImVec2(0, 150), ImGuiChildFlags_Borders);
            for (auto& path : gs.level_files) {
                std::string name = fs::path(path).filename().string();
                if (ImGui::Selectable(name.c_str())) {
                    load_level_fn(path);
                }
            }
            ImGui::EndChild();
        } else {
            ImGui::TextDisabled("No .glb/.gltf files found in levels/");
        }

        ImGui::Spacing();
        ImGui::Text("Or enter a path:");
        ImGui::SetNextItemWidth(-80);
        ImGui::InputText("##lvlpath", gs.level_path_buf, 512);
        ImGui::SameLine();
        if (ImGui::Button("Load") && gs.level_path_buf[0]) {
            load_level_fn(gs.level_path_buf);
        }

        ImGui::Separator();
        ImGui::Text("Procedural Generation");
        if (ImGui::Button("Generate New Level")) {
            gs.procgen_cfg.room_number = gs.rooms_cleared + 1;
            {
                int r = gs.rooms_cleared;
                int tier = r / 10;
                float rate = 0.15f + tier * 0.10f;
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

            // Spawn enemies with difficulty scaling
            {
                float diff = gs.procgen_cfg.difficulty;
                float hp_s = 1.0f + (diff - 1.0f) * (gs.procgen_cfg.hp_scale_per_room / 0.15f);
                float dm_s = 1.0f + (diff - 1.0f) * (gs.procgen_cfg.dmg_scale_per_room / 0.15f);
                float sp_s = 1.0f + (diff - 1.0f) * (gs.procgen_cfg.spd_scale_per_room / 0.15f);

                auto dr = gs.drone_cfg;   dr.drone_health *= hp_s; dr.projectile_damage *= dm_s; dr.chase_speed_min *= sp_s; dr.chase_speed_max *= sp_s; dr.circle_speed_min *= sp_s; dr.circle_speed_max *= sp_s;
                auto ru = gs.rusher_cfg;   ru.health *= hp_s; ru.melee_damage *= dm_s; ru.chase_speed *= sp_s; ru.dash_force *= sp_s;
                auto tu = gs.turret_cfg;   tu.health *= hp_s; tu.beam_dps *= dm_s; tu.track_speed *= sp_s;
                auto tk = gs.tank_cfg;     tk.health *= hp_s; tk.stomp_damage *= dm_s; tk.chase_speed *= sp_s;
                auto bo = gs.bomber_cfg;   bo.health *= hp_s; bo.explosion_damage *= dm_s; bo.approach_speed *= sp_s; bo.dive_speed *= sp_s;
                auto sh = gs.shielder_cfg; sh.health *= hp_s; sh.chase_speed *= sp_s; sh.flee_speed *= sp_s;

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

            gs.current_level_name = "Procedural";
        }
        ImGui::SameLine();
        ImGui::InputInt("Seed", (int*)&gs.procgen_cfg.seed);

        ImGui::SliderFloat2("Room W", &gs.procgen_cfg.room_width_min, 15.0f, 80.0f, "%.0f");
        ImGui::SliderFloat2("Room D", &gs.procgen_cfg.room_depth_min, 15.0f, 80.0f, "%.0f");
        ImGui::SliderFloat("Room Height", &gs.procgen_cfg.room_height, 5.0f, 25.0f, "%.0f");
        ImGui::SliderInt("Boxes Min", &gs.procgen_cfg.box_count_min, 0, 30);
        ImGui::SliderInt("Boxes Max", &gs.procgen_cfg.box_count_max, 0, 30);
        ImGui::SliderInt("Hills Min", &gs.procgen_cfg.hill_count_min, 0, 8);
        ImGui::SliderInt("Hills Max", &gs.procgen_cfg.hill_count_max, 0, 8);
        ImGui::SliderFloat("Hill Height", &gs.procgen_cfg.hill_height_max, 0.5f, 8.0f);
        ImGui::SliderFloat("Hill Radius", &gs.procgen_cfg.hill_radius_max, 4.0f, 30.0f);
        ImGui::SliderFloat("Cluster Chance", &gs.procgen_cfg.cluster_chance, 0.0f, 1.0f);
        ImGui::SliderFloat("Stack Chance", &gs.procgen_cfg.box_stack_chance, 0.0f, 1.0f);
        ImGui::SliderInt("Tall Min", &gs.procgen_cfg.tall_count_min, 0, 15);
        ImGui::SliderInt("Tall Max", &gs.procgen_cfg.tall_count_max, 0, 15);
        ImGui::Text("Room: %d  Difficulty: %.2f", gs.rooms_cleared + 1, gs.procgen_cfg.difficulty);

        ImGui::Separator();
        ImGui::Text("Enemy Budget (0 overrides = random)");
        ImGui::SliderInt("Budget Base", &gs.procgen_cfg.enemy_budget_base, 3, 30);
        ImGui::SliderInt("Budget/Room", &gs.procgen_cfg.enemy_budget_per_room, 0, 5);
        ImGui::SliderInt("Budget Max", &gs.procgen_cfg.enemy_budget_max, 5, 60);
        ImGui::SliderFloat("HP Scale/Room", &gs.procgen_cfg.hp_scale_per_room, 0.0f, 0.2f, "%.2f");
        ImGui::SliderFloat("Dmg Scale/Room", &gs.procgen_cfg.dmg_scale_per_room, 0.0f, 0.2f, "%.2f");
        ImGui::SliderFloat("Spd Scale/Room", &gs.procgen_cfg.spd_scale_per_room, 0.0f, 0.1f, "%.2f");

        ImGui::Separator();
        ImGui::Text("Spawn Weights");
        ImGui::SliderFloat("W Drone", &gs.procgen_cfg.weight_drone, 0.0f, 10.0f);
        ImGui::SliderFloat("W Rusher", &gs.procgen_cfg.weight_rusher, 0.0f, 10.0f);
        ImGui::SliderFloat("W Turret", &gs.procgen_cfg.weight_turret, 0.0f, 10.0f);
        ImGui::SliderFloat("W Tank", &gs.procgen_cfg.weight_tank, 0.0f, 10.0f);
        ImGui::SliderFloat("W Bomber", &gs.procgen_cfg.weight_bomber, 0.0f, 10.0f);
        ImGui::SliderFloat("W Shielder", &gs.procgen_cfg.weight_shielder, 0.0f, 10.0f);

        ImGui::Separator();
        ImGui::Text("Fixed Overrides (all 0 = use budget)");
        ImGui::SliderInt("Drones", &gs.procgen_cfg.drone_count, 0, 20);
        ImGui::SliderInt("Rushers", &gs.procgen_cfg.rusher_count, 0, 20);
        ImGui::SliderInt("Turrets", &gs.procgen_cfg.turret_count, 0, 10);
        ImGui::SliderInt("Tanks", &gs.procgen_cfg.tank_count, 0, 10);
        ImGui::SliderInt("Bombers", &gs.procgen_cfg.bomber_count, 0, 10);
        ImGui::SliderInt("Shielders", &gs.procgen_cfg.shielder_count, 0, 10);
    }

    // ---- Weapons ----
    if (ImGui::CollapsingHeader("Weapons")) {
        for (int w = 0; w < 3; w++) {
            Weapon& wep = gs.weapons[w];
            char label[64];
            snprintf(label, sizeof(label), "%s%s%s", wep.config.name,
                     (w == gs.active_weapon) ? " (active)" : "",
                     (gs.weapon_level[w] == 0) ? " (unowned)" : "");
            if (ImGui::TreeNode(label)) {
                ImGui::PushID(w);
                if (w == gs.active_weapon) {
                    ImGui::Text("State: %s",
                        wep.state == WeaponState::IDLE ? "Idle" :
                        wep.state == WeaponState::FIRING ? "Firing" :
                        wep.state == WeaponState::RELOADING ? "Reloading" :
                        wep.state == WeaponState::SWAPPING ? "Swapping" : "?");
                }
                ImGui::Separator();

                ImGui::SliderFloat("Damage",       &wep.config.damage,     1.0f, 200.0f);
                ImGui::SliderFloat("Fire Rate",    &wep.config.fire_rate,  0.5f, 10.0f, "%.1f shots/s");
                ImGui::SliderFloat("Range",        &wep.config.range,      10.0f, 500.0f);
                ImGui::SliderFloat("Reload Time",  &wep.config.reload_time, 0.5f, 5.0f, "%.1fs");
                ImGui::SliderFloat("Crit Multi",   &wep.config.crit_multiplier, 1.0f, 5.0f, "%.1fx");
                ImGui::Separator();

                ImGui::Text("Viewmodel");
                ImGui::SliderFloat("Model Scale",     &wep.config.model_scale, 0.001f, 5.0f, "%.3f");
                ImGui::SliderFloat3("Model Rotation", &wep.config.model_rotation.X, -180.0f, 180.0f, "%.1f deg");
                ImGui::SliderFloat3("Hip Offset",     &wep.config.hip_offset.X, -1.0f, 1.0f, "%.3f");
                ImGui::SliderFloat3("ADS Offset",     &wep.config.ads_offset.X, -1.0f, 1.0f, "%.3f");
                ImGui::Separator();

                ImGui::Text("ADS");
                ImGui::SliderFloat("ADS FOV Mult",  &wep.config.ads_fov_mult,  0.5f, 1.0f, "%.2f");
                ImGui::SliderFloat("ADS Sens Mult", &wep.config.ads_sens_mult, 0.1f, 1.0f, "%.2f");
                ImGui::SliderFloat("ADS Speed",     &wep.config.ads_speed,     1.0f, 20.0f);
                ImGui::Separator();

                ImGui::Text("Recoil");
                ImGui::SliderFloat("Recoil Kick",     &wep.config.recoil_kick,     0.0f, 0.2f, "%.3f");
                ImGui::SliderFloat("Recoil Pitch",    &wep.config.recoil_pitch,    -60.0f, 60.0f, "%.1f deg");
                ImGui::SliderFloat("Recoil Roll",     &wep.config.recoil_roll,     0.0f, 20.0f, "%.1f deg");
                ImGui::SliderFloat("Recoil Side",     &wep.config.recoil_side,     0.0f, 0.1f, "%.3f");
                ImGui::SliderFloat("Recoil Recovery", &wep.config.recoil_recovery, 1.0f, 30.0f);
                {
                    const char* tilt_items[] = { "Right", "Left" };
                    int tilt_idx = (wep.config.recoil_tilt_dir >= 0.0f) ? 0 : 1;
                    if (ImGui::Combo("Tilt Direction", &tilt_idx, tilt_items, 2))
                        wep.config.recoil_tilt_dir = (tilt_idx == 0) ? 1.0f : -1.0f;
                }
                ImGui::SliderFloat("Reload Buffer Delay", &wep.config.reload_buffer_delay, 0.0f, 1.0f, "%.2fs");
                ImGui::Separator();

                ImGui::Text("Reload Anim (3-phase)");
                ImGui::SliderFloat("Phase1 End (mag out)", &wep.config.reload_phase1, 0.05f, 0.5f, "%.2f");
                ImGui::SliderFloat("Phase2 End (mag swap)", &wep.config.reload_phase2, 0.1f, 0.9f, "%.2f");
                ImGui::SliderFloat("Reload Drop",     &wep.config.reload_drop_dist, 0.0f, 0.5f, "%.3f");
                ImGui::SliderFloat("Reload Tilt",     &wep.config.reload_tilt,      0.0f, 60.0f, "%.1f deg");
                ImGui::SliderFloat("Mag Drop Dist",   &wep.config.mag_drop_dist,    0.0f, 1.0f, "%.3f");
                ImGui::SliderFloat("Mag Insert Dist", &wep.config.mag_insert_dist,  0.0f, 0.5f, "%.3f");
                if (wep.has_mag_submesh)
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Mag sub-mesh: found (%u indices)", wep.mag_index_count);
                else
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "Mag sub-mesh: not found");

                char reset_label[64];
                snprintf(reset_label, sizeof(reset_label), "Reset %s Defaults", wep.config.name);
                if (ImGui::Button(reset_label)) {
                    switch (w) {
                        case 0: wep.init_wingman(); break;
                        case 1: wep.init_glock();   break;
                        case 2: wep.init_knife();   break;
                    }
                }
                ImGui::PopID();
                ImGui::TreePop();
            }
        }
    }

    // ---- Enemies ----
    if (ImGui::CollapsingHeader("Enemies")) {
        ImGui::Checkbox("AI Enabled", &gs.ai_enabled);
        ImGui::SameLine();
        if (ImGui::Button("Kill All")) {
            for (int i = 0; i < gs.max_entities; i++)
                gs.entities[i].alive = false;
        }

        if (ImGui::TreeNode("Drone")) {
            if (ImGui::Button("Spawn Drone")) {
                HMM_Vec3 fwd = gs.camera.forward_flat();
                HMM_Vec3 spawn = HMM_AddV3(gs.player.position, HMM_MulV3F(fwd, 10.0f));
                spawn.Y += 3.0f;
                int idx = drone_spawn(gs.entities, gs.max_entities, spawn, gs.drone_cfg);
                if (idx >= 0) gs.entities[idx].ai_state = DRONE_CHASING;
            }
            ImGui::SliderFloat("Health",          &gs.drone_cfg.drone_health,     1.0f, 200.0f);
            ImGui::SliderFloat("Radius",          &gs.drone_cfg.drone_radius,     0.2f, 2.0f);
            ImGui::SliderFloat("Detection Range", &gs.drone_cfg.detection_range,  5.0f, 100.0f);
            ImGui::SliderFloat("Attack Range",    &gs.drone_cfg.attack_range,     5.0f, 50.0f);
            ImGui::SliderFloat("Circle Distance", &gs.drone_cfg.circle_distance,  3.0f, 20.0f);
            ImGui::SliderFloat("Attack Windup",   &gs.drone_cfg.attack_windup,    0.1f, 3.0f, "%.2fs");
            ImGui::SliderFloat("Acceleration",    &gs.drone_cfg.acceleration,     1.0f, 20.0f);
            ImGui::SliderFloat("Chase Speed Min", &gs.drone_cfg.chase_speed_min,  1.0f, 20.0f);
            ImGui::SliderFloat("Chase Speed Max", &gs.drone_cfg.chase_speed_max,  1.0f, 20.0f);
            ImGui::SliderFloat("Circle Speed Min",&gs.drone_cfg.circle_speed_min, 1.0f, 20.0f);
            ImGui::SliderFloat("Circle Speed Max",&gs.drone_cfg.circle_speed_max, 1.0f, 20.0f);
            ImGui::SliderFloat("Hover Force",     &gs.drone_cfg.hover_force,      1.0f, 30.0f);
            ImGui::SliderFloat("Hover Height Min",&gs.drone_cfg.hover_height_min, 0.5f, 5.0f);
            ImGui::SliderFloat("Hover Height Max",&gs.drone_cfg.hover_height_max, 0.5f, 5.0f);
            ImGui::SliderFloat("Proj Speed",      &gs.drone_cfg.projectile_speed, 5.0f, 50.0f);
            ImGui::SliderFloat("Proj Damage",     &gs.drone_cfg.projectile_damage,1.0f, 50.0f);
            ImGui::SliderFloat("Wander Radius",   &gs.drone_cfg.wander_radius,    2.0f, 20.0f);
            ImGui::SliderFloat("Wall Avoid Dist", &gs.drone_cfg.wall_avoid_dist,  0.5f, 5.0f);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Rusher")) {
            if (ImGui::Button("Spawn Rusher")) {
                HMM_Vec3 fwd = gs.camera.forward_flat();
                HMM_Vec3 spawn = HMM_AddV3(gs.player.position, HMM_MulV3F(fwd, 10.0f));
                spawn.Y += 3.0f;
                int idx = rusher_spawn(gs.entities, gs.max_entities, spawn, gs.rusher_cfg);
                if (idx >= 0) gs.entities[idx].ai_state = RUSHER_CHASING;
            }
            ImGui::SliderFloat("Health",          &gs.rusher_cfg.health,          1.0f, 100.0f);
            ImGui::SliderFloat("Radius",          &gs.rusher_cfg.radius,          0.2f, 2.0f);
            ImGui::SliderFloat("Detection Range", &gs.rusher_cfg.detection_range, 5.0f, 100.0f);
            ImGui::SliderFloat("Melee Damage",    &gs.rusher_cfg.melee_damage,    1.0f, 100.0f);
            ImGui::SliderFloat("Chase Speed",     &gs.rusher_cfg.chase_speed,     5.0f, 30.0f);
            ImGui::SliderFloat("Acceleration",    &gs.rusher_cfg.acceleration,    1.0f, 20.0f);
            ImGui::SliderFloat("Hover Height",    &gs.rusher_cfg.hover_height,    0.5f, 6.0f);
            ImGui::SliderFloat("Attack Range",    &gs.rusher_cfg.attack_range,    2.0f, 20.0f);
            ImGui::SliderFloat("Dash Force",      &gs.rusher_cfg.dash_force,      5.0f, 60.0f);
            ImGui::SliderFloat("Dash Cooldown",   &gs.rusher_cfg.dash_cooldown,   0.5f, 5.0f, "%.1fs");
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Turret")) {
            if (ImGui::Button("Spawn Turret")) {
                HMM_Vec3 spawn = HMM_AddV3(gs.player.position,
                    HMM_MulV3F(gs.camera.forward(), 8.0f));
                int idx = turret_spawn(gs.entities, gs.max_entities, spawn, gs.turret_cfg);
                if (idx >= 0) gs.entities[idx].ai_state = TURRET_TRACKING;
            }
            ImGui::SliderFloat("Health",          &gs.turret_cfg.health,          1.0f, 200.0f);
            ImGui::SliderFloat("Radius",          &gs.turret_cfg.radius,          0.3f, 2.0f);
            ImGui::SliderFloat("Detection Range", &gs.turret_cfg.detection_range, 10.0f, 100.0f);
            ImGui::SliderFloat("Beam DPS",        &gs.turret_cfg.beam_dps,        5.0f, 100.0f);
            ImGui::SliderFloat("Track Speed",     &gs.turret_cfg.track_speed,     0.5f, 10.0f, "%.1f rad/s");
            ImGui::SliderFloat("Windup Time",     &gs.turret_cfg.windup_time,     0.0f, 3.0f, "%.2fs");
            ImGui::SliderFloat("Cooldown Time",   &gs.turret_cfg.cooldown_time,   0.5f, 5.0f, "%.1fs");
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Tank")) {
            if (ImGui::Button("Spawn Tank")) {
                HMM_Vec3 spawn = HMM_AddV3(gs.player.position,
                    HMM_MulV3F(gs.camera.forward(), 8.0f));
                spawn.Y += 3.0f;
                int idx = tank_spawn(gs.entities, gs.max_entities, spawn, gs.tank_cfg);
                if (idx >= 0) gs.entities[idx].ai_state = TANK_CHASING;
            }
            ImGui::SliderFloat("Health",          &gs.tank_cfg.health,            10.0f, 500.0f);
            ImGui::SliderFloat("Radius",          &gs.tank_cfg.radius,            0.5f, 3.0f);
            ImGui::SliderFloat("Detection Range", &gs.tank_cfg.detection_range,   5.0f, 60.0f);
            ImGui::SliderFloat("Chase Speed",     &gs.tank_cfg.chase_speed,       1.0f, 15.0f);
            ImGui::SliderFloat("Stomp Range",     &gs.tank_cfg.stomp_range,       2.0f, 10.0f);
            ImGui::SliderFloat("Stomp Damage",    &gs.tank_cfg.stomp_damage,      5.0f, 200.0f);
            ImGui::SliderFloat("Stomp Cooldown",  &gs.tank_cfg.stomp_cooldown,    1.0f, 10.0f, "%.1fs");
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Bomber")) {
            if (ImGui::Button("Spawn Bomber")) {
                HMM_Vec3 spawn = HMM_AddV3(gs.player.position,
                    HMM_MulV3F(gs.camera.forward(), 8.0f));
                spawn.Y += 5.0f;
                int idx = bomber_spawn(gs.entities, gs.max_entities, spawn, gs.bomber_cfg);
                if (idx >= 0) gs.entities[idx].ai_state = BOMBER_APPROACH;
            }
            ImGui::SliderFloat("Health",           &gs.bomber_cfg.health,          1.0f, 100.0f);
            ImGui::SliderFloat("Radius",           &gs.bomber_cfg.radius,          0.3f, 2.0f);
            ImGui::SliderFloat("Detection Range",  &gs.bomber_cfg.detection_range, 10.0f, 80.0f);
            ImGui::SliderFloat("Approach Speed",   &gs.bomber_cfg.approach_speed,  2.0f, 15.0f);
            ImGui::SliderFloat("Dive Speed",       &gs.bomber_cfg.dive_speed,      5.0f, 30.0f);
            ImGui::SliderFloat("Hover Height",     &gs.bomber_cfg.hover_height,    3.0f, 15.0f);
            ImGui::SliderFloat("Dive Trigger Dist",&gs.bomber_cfg.dive_trigger_dist, 2.0f, 30.0f);
            ImGui::SliderFloat("Explosion Radius", &gs.bomber_cfg.explosion_radius, 2.0f, 15.0f);
            ImGui::SliderFloat("Explosion Damage", &gs.bomber_cfg.explosion_damage, 5.0f, 100.0f);
            ImGui::SliderFloat("Explosion Knockback", &gs.bomber_cfg.explosion_knockback, 5.0f, 80.0f);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Shielder")) {
            if (ImGui::Button("Spawn Shielder")) {
                HMM_Vec3 spawn = HMM_AddV3(gs.player.position,
                    HMM_MulV3F(gs.camera.forward(), 8.0f));
                int idx = shielder_spawn(gs.entities, gs.max_entities, spawn, gs.shielder_cfg);
                if (idx >= 0) gs.entities[idx].ai_state = SHIELDER_CHASING;
            }
            ImGui::SliderFloat("Health",          &gs.shielder_cfg.health,          5.0f, 100.0f);
            ImGui::SliderFloat("Radius",          &gs.shielder_cfg.radius,          0.3f, 2.0f);
            ImGui::SliderFloat("Detection Range", &gs.shielder_cfg.detection_range, 10.0f, 60.0f);
            ImGui::SliderFloat("Shield Radius",   &gs.shielder_cfg.shield_radius,   3.0f, 20.0f);
            ImGui::SliderFloat("Shield HP",       &gs.shielder_cfg.shield_hp,       5.0f, 50.0f);
            ImGui::SliderFloat("Recharge/s",      &gs.shielder_cfg.shield_recharge, 1.0f, 20.0f);
            ImGui::SliderFloat("Apply Cooldown",  &gs.shielder_cfg.shield_apply_cd, 0.5f, 10.0f, "%.1fs");
            ImGui::SliderFloat("Flee Range",      &gs.shielder_cfg.flee_range,      3.0f, 15.0f);
            ImGui::SliderFloat("Preferred Dist",  &gs.shielder_cfg.preferred_dist,  5.0f, 25.0f);
            ImGui::SliderFloat("Chase Speed",     &gs.shielder_cfg.chase_speed,     2.0f, 15.0f);
            ImGui::SliderFloat("Flee Speed",      &gs.shielder_cfg.flee_speed,      3.0f, 15.0f);
            ImGui::TreePop();
        }
    }

    // ---- Mouse ----
    if (ImGui::CollapsingHeader("Mouse", ImGuiTreeNodeFlags_DefaultOpen)) {
        float sens_display = gs.camera.sensitivity * 10000.0f;
        if (ImGui::SliderFloat("Sensitivity", &sens_display, 0.1f, 50.0f, "%.1f"))
            gs.camera.sensitivity = sens_display / 10000.0f;

        bool inv_x = gs.camera.invert_x < 0.0f;
        bool inv_y = gs.camera.invert_y < 0.0f;
        if (ImGui::Checkbox("Invert X (horizontal)", &inv_x))
            gs.camera.invert_x = inv_x ? -1.0f : 1.0f;
        if (ImGui::Checkbox("Invert Y (vertical)", &inv_y))
            gs.camera.invert_y = inv_y ? -1.0f : 1.0f;
    }

    // ---- Video ----
    if (ImGui::CollapsingHeader("Video")) {
        ImGui::SliderFloat("FOV", &gs.camera.fov, 60.0f, 130.0f, "%.0f");
        ImGui::Checkbox("Damage Numbers", &gs.show_damage_numbers);
    }

    // ---- Keybinds ----
    if (ImGui::CollapsingHeader("Keybinds", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("Click a slot to rebind. Press a key, mouse button, or scroll wheel.");
        ImGui::TextDisabled("Escape cancels. Right-click a slot to clear it.");
        ImGui::Spacing();

        for (int i = 0; i < ACTION_COUNT; i++) {
            ImGui::PushID(i);

            Action action = static_cast<Action>(i);
            const char* name = action_name(action);

            ImGui::AlignTextToFramePadding();
            ImGui::Text("%-16s", name);

            for (int s = 0; s < SLOTS; s++) {
                ImGui::SameLine(s == 0 ? 160.0f : 300.0f);
                ImGui::PushID(s);

                if (gs.rebinding_action == i && gs.rebinding_slot == s) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.3f, 0.1f, 1.0f));
                    ImGui::Button("...press...");
                    ImGui::PopStyleColor();
                } else {
                    InputCode code = gs.kb.get(action, s);
                    char label[64];
                    snprintf(label, sizeof(label), "  %s  ", input_code_name(code));
                    if (ImGui::Button(label)) {
                        gs.rebinding_action = i;
                        gs.rebinding_slot = s;
                    }
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                        gs.kb.set(action, s, INPUT_NONE);
                    }
                }

                ImGui::PopID();
            }

            ImGui::PopID();
        }

        ImGui::Spacing();
        if (ImGui::Button("Reset Keybinds to Defaults")) {
            gs.kb.reset_defaults();
            gs.rebinding_action = -1;
        }
    }

    // ---- Movement ----
    if (ImGui::CollapsingHeader("Movement")) {
        ImGui::SliderFloat("Gravity",          &gs.player.gravity,        0.0f, 40.0f);
        ImGui::SliderFloat("Max Speed (Holstered)", &gs.player.max_speed,    1.0f, 30.0f);
        ImGui::SliderFloat("Weapon Speed",     &gs.player.weapon_speed,   1.0f, 30.0f);
        ImGui::SliderFloat("Air Wish Speed",   &gs.player.air_wish_speed, 0.1f, 5.0f, "%.2f");
        ImGui::SliderFloat("Ground Accel",     &gs.player.ground_accel,   1.0f, 50.0f);
        ImGui::Text("Holstered: %s (effective: %.1f u/s, accel: %.1f)",
                    gs.player.weapon_holstered ? "YES" : "NO",
                    gs.player.effective_max_speed(), gs.player.effective_accel());
        ImGui::SliderFloat("Air Accel",        &gs.player.air_accel,      1.0f, 200.0f);
        ImGui::SliderFloat("Friction",         &gs.player.friction,       0.0f, 20.0f);
        ImGui::SliderFloat("Jump Speed",       &gs.player.jump_speed,     1.0f, 20.0f);

        ImGui::Spacing();
        ImGui::Checkbox("Auto-hop (hold jump to bhop)", &gs.player.auto_hop);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Slide");
        ImGui::SliderFloat("Slide Friction",      &gs.player.slide_friction,         0.0f, 5.0f, "%.2f");
        ImGui::SliderFloat("Slide Boost",         &gs.player.slide_boost,            0.0f, 10.0f);
        ImGui::SliderFloat("Slide Min Speed",     &gs.player.slide_min_speed,        0.0f, 15.0f);
        ImGui::SliderFloat("Slide Stop Speed",    &gs.player.slide_stop_speed,       0.0f, 10.0f);
        ImGui::SliderFloat("Slide Boost Cooldown",&gs.player.slide_boost_cooldown,   0.0f, 5.0f);
        ImGui::SliderFloat("Slide Jump Boost",    &gs.player.slide_jump_boost,       0.0f, 10.0f);
        ImGui::Separator();
        ImGui::Text("Speed Cap");
        ImGui::SliderFloat("Soft Speed Cap",     &gs.player.soft_speed_cap,     0.0f, 30.0f);
        ImGui::SliderFloat("Drag Min",           &gs.player.soft_cap_drag_min,  0.0f, 5.0f);
        ImGui::SliderFloat("Drag Max",           &gs.player.soft_cap_drag_max,  0.0f, 10.0f);
        ImGui::SliderFloat("Drag Full Speed",    &gs.player.soft_cap_drag_full, 0.0f, 30.0f);
        ImGui::SliderFloat("Hard Speed Cap",     &gs.player.hard_speed_cap,     10.0f, 100.0f);
        ImGui::SliderFloat("Slope Land Convert",  &gs.player.slope_landing_conversion, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Crouch Speed",        &gs.player.crouch_speed,           1.0f, 10.0f);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Lurch");
        ImGui::SliderFloat("Lurch Window",    &gs.player.lurch_window,   0.0f, 2.0f, "%.2f");
        ImGui::SliderFloat("Lurch Strength",  &gs.player.lurch_strength, 0.0f, 1.0f, "%.2f");

        ImGui::Spacing();
        if (ImGui::Button("Reset to Source defaults")) {
            gs.player.gravity        = 20.0f;
            gs.player.max_speed      = 8.0f;
            gs.player.air_wish_speed = 0.76f;
            gs.player.ground_accel   = 10.0f;
            gs.player.air_accel      = 70.0f;
            gs.player.friction       = 6.0f;
            gs.player.jump_speed     = 7.2f;
            gs.player.slide_friction = 0.4f;
            gs.player.slide_boost    = 3.0f;
            gs.player.slide_min_speed = 6.0f;
            gs.player.slide_stop_speed = 3.0f;
            gs.player.slide_boost_cooldown = 2.0f;
            gs.player.slide_jump_boost = 4.0f;
            gs.player.slope_landing_conversion = 0.5f;
            gs.player.crouch_speed   = 4.0f;
            gs.player.lurch_window   = 0.5f;
            gs.player.lurch_strength = 0.5f;
        }
    }

    // ---- Ladder ----
    if (ImGui::CollapsingHeader("Ladder")) {
        ImGui::SliderFloat("Climb Speed Mult",  &gs.player.ladder_speed_mult, 0.1f, 2.0f, "%.2fx");
        ImGui::SliderFloat("Jump Off Mult",    &gs.player.ladder_jump_mult,  0.5f, 3.0f, "%.2fx");
        ImGui::Text("Climb: %.1f u/s  Jump-off: %.1f u/s",
                    gs.player.max_speed * gs.player.ladder_speed_mult,
                    gs.player.max_speed * gs.player.ladder_jump_mult);
        ImGui::SliderFloat("Ladder Inflate",  &gs.collision.ladder_inflate, 0.0f, 2.0f, "%.2f");
        ImGui::Checkbox("Show Ladder Volumes", &gs.show_ladder_debug);
        ImGui::Text("Ladder volumes: %zu", gs.collision.ladder_volumes.size());
    }

    ImGui::Spacing();
    ImGui::Separator();

    if (ImGui::Button("Save Settings")) {
        gs.config.pull(gs.camera, gs.player);
        gs.config.save();
    }
    ImGui::SameLine();
    if (ImGui::Button("Load Settings")) {
        gs.config.load();
        gs.config.apply(gs.camera, gs.player);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.15f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
    if (ImGui::Button("Quit Game")) {
        gs.running = false;
    }
    ImGui::PopStyleColor(2);

    ImGui::End();

    // Handle close (user clicked X or show_settings was toggled off)
    if (!gs.show_settings) {
        SDL_SetWindowRelativeMouseMode(gs.window, true);
        gs.rebinding_action = -1;
        gs.rebinding_slot = -1;
    }
}
