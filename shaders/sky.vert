#version 450

layout(push_constant) uniform PC {
    mat4 viewProj;
    vec4 cam;
    uint tex;
    uint nrmSpc;
} pc;

layout(location = 0) out vec3 dir;

void main() {
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vec4 ndc = vec4(p * 2.0 - 1.0, 1.0, 1.0);
    vec4 w = inverse(pc.viewProj) * ndc;
    dir = w.xyz / w.w - pc.cam.xyz;
    gl_Position = vec4(ndc.xy, 1.0, 1.0);
}
