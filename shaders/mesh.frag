#version 450

layout(location = 0) in vec3 wpos;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PC {
    mat4 viewProj;
    vec4 cam;
} pc;

void main() {
    // flat shading from screen-space derivatives: the viewer has no normals or textures yet
    vec3 n = normalize(cross(dFdx(wpos), dFdy(wpos)));
    float sun = max(dot(n, normalize(vec3(0.4, 0.8, 0.3))), 0.0);
    float sky = 0.5 + 0.5 * n.y;
    vec3 base = mix(vec3(0.55, 0.52, 0.48), vec3(0.62, 0.66, 0.58), clamp(n.y, 0.0, 1.0));
    vec3 col = base * (0.25 + 0.45 * sky + 0.55 * sun);
    float d = distance(wpos, pc.cam.xyz);
    float fog = 1.0 - exp(-d * pc.cam.w);
    outColor = vec4(mix(col, vec3(0.72, 0.80, 0.88), clamp(fog, 0.0, 1.0)), 1.0);
}
