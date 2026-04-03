#pragma once

#include "vendor/HandmadeMath.h"

// ============================================================
//  Shared geometric types used by collision, BVH, etc.
// ============================================================

struct Triangle {
    HMM_Vec3 v0, v1, v2;
    HMM_Vec3 normal;
};

struct HitResult {
    bool     hit      = false;
    float    t        = 1e30f;      // parametric distance along ray
    HMM_Vec3 point    = {};         // world-space hit point
    HMM_Vec3 normal   = {};         // surface normal at hit
};
