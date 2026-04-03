#version 450

layout(location = 0) in vec2 frag_uv;
layout(location = 1) in vec4 frag_color;
layout(location = 2) in float frag_type;
layout(location = 3) in float frag_time;

layout(location = 0) out vec4 out_color;

// ============================================================
//  Ring effect: glowing expanding ring
//  (port of DroneExplosionRingSimple.shader)
// ============================================================

vec4 ring_effect(vec2 uv, vec4 tint) {
    float dist = length(uv - 0.5) * 2.0;

    // Ring shape: bright at a specific radius, falls off
    float ring_radius = 0.7;
    float ring_width  = 0.25;
    float ring = 1.0 - smoothstep(0.0, ring_width, abs(dist - ring_radius));

    // Glow falloff from center
    float glow = exp(-dist * 2.0) * 0.5;

    float intensity = (ring + glow) * tint.a;
    vec3 color = tint.rgb * 2.0 * intensity;

    return vec4(color, intensity);
}

// ============================================================
//  Particle effect: glowing blob with UV distortion
//  (port of DroneExplosionParticle.shader)
// ============================================================

vec4 particle_effect(vec2 uv, vec4 tint, float time) {
    // UV distortion
    float distortion_speed = 3.0;
    float distortion_amount = 0.08;
    vec2 distorted_uv = uv + vec2(
        sin(uv.y * 10.0 + time * distortion_speed) * distortion_amount,
        cos(uv.x * 10.0 + time * distortion_speed) * distortion_amount
    );

    // Soft circle
    float dist = length(distorted_uv - 0.5) * 2.0;
    float alpha = 1.0 - smoothstep(0.0, 1.0, dist);
    alpha *= alpha; // sharper falloff

    // Edge glow (brighter near edges of the circle)
    float edge_power = 1.0 - dist;
    edge_power = clamp(edge_power * 2.0, 0.0, 1.0);

    vec3 glow_color = tint.rgb * (1.0 + edge_power) * 2.0;
    float final_alpha = alpha * tint.a;

    return vec4(glow_color * final_alpha, final_alpha);
}

// ============================================================

void main() {
    vec4 result;

    if (frag_type < 0.5) {
        result = ring_effect(frag_uv, frag_color);
    } else {
        result = particle_effect(frag_uv, frag_color, frag_time);
    }

    // Discard nearly invisible fragments
    if (result.a < 0.01) discard;

    out_color = result;
}
