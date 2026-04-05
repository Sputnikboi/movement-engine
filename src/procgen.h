#pragma once

#include "level_loader.h"
#include "vendor/HandmadeMath.h"

struct ProcGenConfig {
    // Room dimensions
    float room_width_min   = 30.0f;
    float room_width_max   = 50.0f;
    float room_depth_min   = 30.0f;
    float room_depth_max   = 50.0f;
    float room_height      = 12.0f;
    float wall_thickness   = 0.5f;

    // Boxes / cover
    int   box_count_min    = 5;
    int   box_count_max    = 15;
    float box_size_min     = 1.0f;
    float box_size_max     = 4.0f;
    float box_height_min   = 1.0f;
    float box_height_max   = 5.0f;
    float box_margin       = 2.0f;   // min distance from walls

    // Platforms / raised areas
    int   platform_count   = 2;
    float platform_height_min = 2.0f;
    float platform_height_max = 4.0f;
    float platform_size_min   = 5.0f;
    float platform_size_max   = 10.0f;

    // Ramps (connect floor to platforms)
    bool  gen_ramps        = true;
    float ramp_width       = 3.0f;

    // Enemies
    int   drone_count      = 3;
    int   rusher_count     = 2;
    float enemy_height     = 3.0f;   // spawn height above floor

    // Colors
    HMM_Vec3 floor_color   = {0.3f, 0.3f, 0.35f};
    HMM_Vec3 wall_color    = {0.25f, 0.25f, 0.3f};
    HMM_Vec3 ceiling_color = {0.2f, 0.2f, 0.25f};
    HMM_Vec3 box_color     = {0.4f, 0.35f, 0.3f};
    HMM_Vec3 platform_color = {0.35f, 0.3f, 0.35f};
    HMM_Vec3 ramp_color    = {0.35f, 0.35f, 0.3f};

    // Seed (0 = random)
    unsigned int seed      = 0;
};

// Generate a procedural level. Returns LevelData with mesh,
// spawn point, and enemy spawn positions.
LevelData generate_level(const ProcGenConfig& config);
