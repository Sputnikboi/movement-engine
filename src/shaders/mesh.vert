#version 450

layout(set = 0, binding = 0) uniform SceneData {
    mat4 view;
    mat4 projection;
    vec4 light_dir;     // xyz = direction (toward light), w = unused
    vec4 camera_pos;    // xyz = world position, w = unused
} scene;

layout(push_constant) uniform PushConstants {
    mat4 model;
} pc;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_color;

layout(location = 0) out vec3 frag_world_pos;
layout(location = 1) out vec3 frag_normal;
layout(location = 2) out vec3 frag_color;

void main() {
    vec4 world_pos = pc.model * vec4(in_position, 1.0);
    frag_world_pos = world_pos.xyz;
    frag_normal    = mat3(pc.model) * in_normal;  // correct for uniform scale
    frag_color     = in_color;
    gl_Position    = scene.projection * scene.view * world_pos;
}
