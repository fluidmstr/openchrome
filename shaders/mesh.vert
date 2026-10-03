#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec3 iPos;
layout(location = 3) in vec3 iScale;
layout(location = 4) in vec4 iQuat;

layout(push_constant) uniform PC {
    mat4 viewProj;
    vec4 cam;
    uint tex;
} pc;

layout(location = 0) out vec3 wpos;
layout(location = 1) out vec2 uv;

vec3 rotate(vec4 q, vec3 v) { return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v); }

void main() {
    vec3 w = rotate(iQuat, inPos * iScale) + iPos;
    wpos = w;
    uv = inUv;
    gl_Position = pc.viewProj * vec4(w, 1.0);
}
