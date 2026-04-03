#version 450

layout(set = 0, binding = 0) uniform SceneData {
    mat4 view;
    mat4 projection;
    vec4 light_dir;
    vec4 camera_pos;
} scene;

// Per-particle data packed into vertex attributes
layout(location = 0) in vec3 in_center;    // world-space particle center
layout(location = 1) in vec2 in_corner;    // which corner: (-1,-1), (1,-1), (1,1), (-1,1)
layout(location = 2) in vec4 in_color;     // RGBA tint (alpha = fade)
layout(location = 3) in vec2 in_params;    // x = size, y = type (0=ring, 1=particle)

layout(location = 0) out vec2 frag_uv;
layout(location = 1) out vec4 frag_color;
layout(location = 2) out float frag_type;
layout(location = 3) out float frag_time;

layout(push_constant) uniform PushConstants {
    float time;
} pc;

void main() {
    // Billboard: extract camera right and up from view matrix
    vec3 cam_right = vec3(scene.view[0][0], scene.view[1][0], scene.view[2][0]);
    vec3 cam_up    = vec3(scene.view[0][1], scene.view[1][1], scene.view[2][1]);

    float size = in_params.x;
    vec3 world_pos = in_center
                   + cam_right * in_corner.x * size
                   + cam_up    * in_corner.y * size;

    gl_Position = scene.projection * scene.view * vec4(world_pos, 1.0);

    frag_uv    = in_corner * 0.5 + 0.5;  // map from [-1,1] to [0,1]
    frag_color = in_color;
    frag_type  = in_params.y;
    frag_time  = pc.time;
}
