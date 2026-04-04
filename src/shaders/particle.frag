#version 450

layout(location = 0) in vec2 frag_uv;
layout(location = 1) in vec4 frag_color;
layout(location = 2) in float frag_type;
layout(location = 3) in float frag_time;

layout(location = 0) out vec4 out_color;

// ============================================================
//  Ring effect: solid donut ring
// ============================================================

vec4 ring_effect(vec2 uv, vec4 tint) {
    float dist = length(uv - 0.5) * 2.0;

    // Solid donut: hard inner and outer edges with slight softness
    float ring_radius = 0.65;
    float ring_half_width = 0.18;
    float inner = smoothstep(ring_radius - ring_half_width - 0.05,
                             ring_radius - ring_half_width, dist);
    float outer = 1.0 - smoothstep(ring_radius + ring_half_width,
                                    ring_radius + ring_half_width + 0.05, dist);
    float ring = inner * outer;

    float intensity = ring * tint.a;
    vec3 color = tint.rgb * 1.5 * intensity;

    return vec4(color, intensity);
}

// ============================================================
//  Ball effect: solid glowing ball that collapses
// ============================================================

vec4 particle_effect(vec2 uv, vec4 tint, float time) {
    float dist = length(uv - 0.5) * 2.0;

    // Solid ball with soft edge
    float alpha = 1.0 - smoothstep(0.6, 1.0, dist);

    // Bright core
    float core = exp(-dist * 3.0) * 0.5;
    alpha = clamp(alpha + core, 0.0, 1.0);

    vec3 color = tint.rgb * (1.0 + core) * 1.5;
    float final_alpha = alpha * tint.a;

    return vec4(color * final_alpha, final_alpha);
}

// ============================================================

void main() {
    vec4 result;

    if (frag_type < 0.5) {
        result = ring_effect(frag_uv, frag_color);
    } else {
        result = particle_effect(frag_uv, frag_color, frag_time);
    }

    if (result.a < 0.01) discard;

    out_color = result;
}
