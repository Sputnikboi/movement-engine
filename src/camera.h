#pragma once

#include "vendor/HandmadeMath.h"
#include <algorithm>
#include <cmath>

struct Camera {
    HMM_Vec3 position = HMM_V3(0.0f, 3.0f, 10.0f);
    float yaw   = -HMM_PI32 / 2.0f;   // start looking along -Z
    float pitch  = 0.0f;
    float fov    = 90.0f;               // degrees
    float near_plane = 0.1f;
    float far_plane  = 500.0f;
    float sensitivity = 0.00014f;

    // --- Derived directions ---

    HMM_Vec3 forward() const {
        return HMM_V3(
            cosf(pitch) * cosf(yaw),
            sinf(pitch),
            cosf(pitch) * sinf(yaw)
        );
    }

    // Forward projected onto XZ plane (for movement)
    HMM_Vec3 forward_flat() const {
        return HMM_NormV3(HMM_V3(cosf(yaw), 0.0f, sinf(yaw)));
    }

    HMM_Vec3 right() const {
        return HMM_NormV3(HMM_Cross(forward(), HMM_V3(0.0f, 1.0f, 0.0f)));
    }

    // --- Matrices ---

    HMM_Mat4 view_matrix() const {
        HMM_Vec3 fwd = forward();
        HMM_Vec3 center = HMM_AddV3(position, fwd);
        return HMM_LookAt_RH(position, center, HMM_V3(0.0f, 1.0f, 0.0f));
    }

    HMM_Mat4 projection_matrix(float aspect) const {
        float fov_rad = fov * (HMM_PI32 / 180.0f);
        HMM_Mat4 proj = HMM_Perspective_RH_ZO(fov_rad, aspect, near_plane, far_plane);
        proj.Elements[1][1] *= -1.0f;  // flip Y for Vulkan
        return proj;
    }

    // --- Input ---

    void mouse_look(float dx, float dy) {
        yaw   -= dx * sensitivity;
        pitch += dy * sensitivity;  // moving mouse up looks up
        pitch = std::clamp(pitch, -HMM_PI32 / 2.0f + 0.01f, HMM_PI32 / 2.0f - 0.01f);
    }
};
