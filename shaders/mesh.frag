#version 450

layout(set = 0, binding = 0) uniform sampler2D textures[4096];

layout(location = 0) in vec3 wpos;
layout(location = 1) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PC {
    mat4 viewProj;
    vec4 cam;
    uint tex;
} pc;

void main() {
    // flat shading from screen-space derivatives: the viewer has no normals yet
    vec3 n = normalize(cross(dFdx(wpos), dFdy(wpos)));
    float sun = max(dot(n, normalize(vec3(0.4, 0.8, 0.3))), 0.0);
    float sky = 0.5 + 0.5 * n.y;
    vec3 albedo = texture(textures[pc.tex], uv).rgb;
    vec3 col = albedo * (0.35 + 0.35 * sky + 0.5 * sun);
    float d = distance(wpos, pc.cam.xyz);
    float fog = 1.0 - exp(-d * pc.cam.w);
    outColor = vec4(mix(col, vec3(0.72, 0.80, 0.88), clamp(fog, 0.0, 1.0)), 1.0);
}
