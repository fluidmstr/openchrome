#version 450

layout(location = 0) in vec3 color;
layout(location = 0) out vec4 outColor;

void main() {
    vec2 p = gl_PointCoord * 2.0 - 1.0;
    float r2 = dot(p, p);
    if (r2 > 1.0) discard;
    float a = pow(1.0 - r2, 3.0);
    outColor = vec4(color * a * 1.5, a);
}
