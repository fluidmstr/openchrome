#version 450

layout(set = 0, binding = 0) uniform sampler2D textures[4096];

layout(set = 1, binding = 0) uniform Frame { mat4 lightVP; vec4 sun; vec4 params; } frame;
layout(set = 1, binding = 1) uniform sampler2DShadow shadowMap;

layout(location = 0) in vec3 wpos;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec3 wnormal;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PC {
    mat4 viewProj;
    vec4 cam;
    uint tex;
} pc;

vec3 aces(vec3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }

float shadowFactor(vec3 N) {
    vec4 lp = frame.lightVP * vec4(wpos + N * 0.12, 1.0);
    vec3 sc = lp.xyz / lp.w;
    sc.xy = sc.xy * 0.5 + 0.5;
    if (sc.x < 0.0 || sc.x > 1.0 || sc.y < 0.0 || sc.y > 1.0 || sc.z > 1.0) return 1.0;
    float t = frame.params.x, s = 0.0;
    for (int x = -1; x <= 1; x++)
        for (int y = -1; y <= 1; y++) s += texture(shadowMap, vec3(sc.xy + vec2(x, y) * t, sc.z));
    return s / 9.0;
}

void main() {
    // meshes without decoded normals fall back to the flat normal from screen-space derivatives
    vec3 N = dot(wnormal, wnormal) > 0.01 ? normalize(wnormal) : normalize(cross(dFdx(wpos), dFdy(wpos)));
    vec3 sunDir = frame.sun.xyz;
    float ndl = max(dot(N, sunDir), 0.0) * shadowFactor(N);
    vec3 skyCol = vec3(0.42, 0.55, 0.78), groundCol = vec3(0.28, 0.24, 0.20);
    vec3 hemi = mix(groundCol, skyCol, N.y * 0.5 + 0.5);
    vec3 albedo = pow(texture(textures[pc.tex], uv).rgb, vec3(2.2));
    vec3 col = albedo * (hemi * 0.7 + vec3(1.0, 0.93, 0.80) * 2.0 * ndl);
    float d = distance(wpos, pc.cam.xyz);
    float fog = 1.0 - exp(-d * pc.cam.w);
    col = mix(col, vec3(0.55, 0.65, 0.78), clamp(fog, 0.0, 1.0));
    outColor = vec4(pow(aces(col * 0.85), vec3(1.0 / 2.2)), 1.0);
}
