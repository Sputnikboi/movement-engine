#version 450

layout(set = 0, binding = 0) uniform SceneData {
    mat4 view;
    mat4 projection;
    vec4 light_dir;
    vec4 camera_pos;
} scene;

layout(location = 0) in vec3 frag_world_pos;
layout(location = 1) in vec3 frag_normal;
layout(location = 2) in vec3 frag_color;

layout(location = 0) out vec4 out_color;

void main() {
    float normal_len = length(frag_normal);

    // Emissive flag: zero-length normal means self-lit (no lighting/specular)
    if (normal_len < 0.01) {
        // Pure emissive — color is output directly, boosted for glow
        vec3 color = frag_color;

        // Distance fog (still apply so it doesn't pop)
        float dist = length(scene.camera_pos.xyz - frag_world_pos);
        float fog  = clamp((dist - 40.0) / 60.0, 0.0, 1.0);
        vec3 fog_color = vec3(0.02, 0.02, 0.03);
        color = mix(color, fog_color, fog);

        out_color = vec4(color, 1.0);
        return;
    }

    vec3 N = normalize(frag_normal);
    vec3 L = normalize(scene.light_dir.xyz);

    // Ambient
    float ambient = 0.12;

    // Diffuse (half-lambert for softer look — avoids pure-black shadows)
    float ndotl = dot(N, L);
    float diffuse = ndotl * 0.5 + 0.5;
    diffuse *= diffuse;  // square for contrast
    diffuse *= 0.75;     // scale down

    // Specular (Blinn-Phong)
    vec3 V = normalize(scene.camera_pos.xyz - frag_world_pos);
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 64.0) * 0.25;

    vec3 color = frag_color * (ambient + diffuse) + vec3(spec);

    // Simple distance fog
    float dist = length(scene.camera_pos.xyz - frag_world_pos);
    float fog  = clamp((dist - 40.0) / 60.0, 0.0, 1.0);
    vec3 fog_color = vec3(0.02, 0.02, 0.03);
    color = mix(color, fog_color, fog);

    out_color = vec4(color, 1.0);
}
