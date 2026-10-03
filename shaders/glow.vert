#version 450

layout(set = 1, binding = 0) uniform Frame {
    mat4 lightVP;
    vec4 sunDir, sunColor, skyColor, groundColor, fogColor;
    vec4 params;   // x shadow texel, yz light grid origin, w cell size
    vec4 params2;  // x grid w, y grid h, z light count, w exposure
} frame;
layout(std430, set = 1, binding = 2) readonly buffer Lights { vec4 L[]; } lights;

layout(push_constant) uniform PC {
    mat4 viewProj;
    vec4 cam;
    uint tex;
} pc;

layout(location = 0) out vec3 color;

void main() {
    vec4 a = lights.L[2 * gl_VertexIndex], b = lights.L[2 * gl_VertexIndex + 1];
    float dist = distance(a.xyz, pc.cam.xyz);
    gl_Position = pc.viewProj * vec4(a.xyz, 1.0);
    gl_PointSize = clamp(600.0 * (0.3 + 0.025 * a.w) / max(dist, 1.0), 2.0, 96.0);
    color = b.rgb * min(b.w, 3.0) * frame.sunColor.w;
    if (dist > 320.0 || frame.sunColor.w < 0.02) gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
}
