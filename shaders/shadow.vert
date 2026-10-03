#version 450

layout(location = 0) in vec3 inPos;
layout(location = 3) in vec3 iPos;
layout(location = 4) in vec3 iScale;
layout(location = 5) in vec4 iQuat;

layout(push_constant) uniform PC { mat4 lightVP; } pc;

vec3 rotate(vec4 q, vec3 v) { return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v); }

void main() {
    gl_Position = pc.lightVP * vec4(rotate(iQuat, inPos * iScale) + iPos, 1.0);
}
