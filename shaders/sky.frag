#version 450

layout(set = 1, binding = 0) uniform Frame {
    mat4 lightVP;
    vec4 sunDir, sunColor, skyColor, groundColor, fogColor;
    vec4 params;
    vec4 params2;  // w exposure
} frame;

layout(location = 0) in vec3 dir;
layout(location = 0) out vec4 outColor;

vec3 aces(vec3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }
float hash(vec3 p) { p = fract(p * 0.3183099 + 0.1); p *= 17.0; return fract(p.x * p.y * p.z * (p.x + p.y + p.z)); }

void main() {
    vec3 d = normalize(dir);
    float up = clamp(d.y, 0.0, 1.0);
    vec3 horizon = frame.fogColor.rgb;
    vec3 zenith = frame.skyColor.rgb * 1.5;
    vec3 col = mix(horizon, zenith, pow(up, 0.45));
    if (d.y < 0.0) col = mix(horizon, frame.groundColor.rgb, clamp(-d.y * 4.0, 0.0, 1.0));
    float cs = max(dot(d, frame.sunDir.xyz), 0.0);
    col += frame.sunColor.rgb * (pow(cs, 8.0) * 0.25 + pow(cs, 400.0) * 40.0);
    float night = frame.sunColor.w;
    if (night > 0.02 && d.y > 0.0) {
        vec3 cell = floor(d * 220.0);
        float s = step(0.9975, hash(cell));
        col += vec3(0.8, 0.85, 1.0) * s * night * smoothstep(0.05, 0.4, d.y) * 1.5;
    }
    outColor = vec4(pow(aces(col * frame.params2.w), vec3(1.0 / 2.2)), 1.0);
}
