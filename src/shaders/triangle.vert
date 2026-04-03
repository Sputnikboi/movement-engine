#version 450

layout(push_constant) uniform PushConstants {
    float time;
} pc;

layout(location = 0) in vec2 in_position;
layout(location = 1) in vec3 in_color;

layout(location = 0) out vec3 frag_color;

void main() {
    float c = cos(pc.time);
    float s = sin(pc.time);
    vec2 rotated = vec2(
        in_position.x * c - in_position.y * s,
        in_position.x * s + in_position.y * c
    );
    gl_Position = vec4(rotated, 0.0, 1.0);
    frag_color = in_color;
}
