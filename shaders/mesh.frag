#version 450

layout(set = 0, binding = 0) uniform sampler2D textures[4096];

layout(set = 1, binding = 0) uniform Frame {
    mat4 lightVP;
    vec4 sunDir, sunColor, skyColor, groundColor, fogColor;
    vec4 params;   // x shadow texel, yz light grid origin, w cell size
    vec4 params2;  // x grid w, y grid h, z light count, w exposure
} frame;
layout(set = 1, binding = 1) uniform sampler2DShadow shadowMap;
layout(std430, set = 1, binding = 2) readonly buffer Lights { vec4 L[]; } lights;
layout(std430, set = 1, binding = 3) readonly buffer Cells { uint start[]; } cells;
layout(std430, set = 1, binding = 4) readonly buffer Idx { uint v[]; } lidx;

layout(location = 0) in vec3 wpos;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec3 wnormal;
layout(location = 3) in float emissive;
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

// point lights from the map: only the lights registered in this 16 m grid cell are visited
vec3 pointLights(vec3 N) {
    ivec2 c = ivec2(floor((wpos.xz - frame.params.yz) / frame.params.w));
    int gw = int(frame.params2.x), gh = int(frame.params2.y);
    if (c.x < 0 || c.y < 0 || c.x >= gw || c.y >= gh) return vec3(0.0);
    uint cell = uint(c.y * gw + c.x);
    vec3 acc = vec3(0.0);
    for (uint k = cells.start[cell]; k < cells.start[cell + 1]; k++) {
        uint li = lidx.v[k];
        vec4 a = lights.L[2 * li], b = lights.L[2 * li + 1];
        vec3 d = a.xyz - wpos;
        float d2 = dot(d, d), R = a.w;
        if (d2 > R * R) continue;
        float dist = sqrt(d2);
        float w = clamp(1.0 - pow(dist / R, 4.0), 0.0, 1.0);
        float att = w * w / (d2 + 1.0);
        float ndl = max(dot(N, d / max(dist, 1e-3)), 0.0) * 0.8 + 0.2;
        acc += b.rgb * min(b.w, 4.0) * att * ndl;
    }
    return acc * 14.0;
}

void main() {
    // meshes without decoded normals fall back to the flat normal from screen-space derivatives
    vec3 N = dot(wnormal, wnormal) > 0.01 ? normalize(wnormal) : normalize(cross(dFdx(wpos), dFdy(wpos)));
    float ndl = max(dot(N, frame.sunDir.xyz), 0.0) * shadowFactor(N);
    vec3 hemi = mix(frame.groundColor.rgb, frame.skyColor.rgb, N.y * 0.5 + 0.5);
    vec3 albedo = pow(texture(textures[pc.tex], uv).rgb, vec3(2.2));
    vec3 col = albedo * (hemi * 0.7 + frame.sunColor.rgb * ndl);
    col += albedo * pointLights(N) * frame.sunColor.w;
    // lit windows / glowing signs after dusk
    col += mix(albedo, vec3(1.0, 0.72, 0.42), 0.55) * emissive * 2.2 * frame.sunColor.w;
    float d = distance(wpos, pc.cam.xyz);
    float fog = 1.0 - exp(-d * pc.cam.w);
    col = mix(col, frame.fogColor.rgb, clamp(fog, 0.0, 1.0));
    outColor = vec4(pow(aces(col * frame.params2.w), vec3(1.0 / 2.2)), 1.0);
}
