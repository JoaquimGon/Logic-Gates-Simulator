#version 330 core
#include "../common/outline.glsl"
out vec4 FragColor;
in vec4 instanceTint;
in vec2 localPos;

void main()
{
    // One-cell diameter with space around the quad for antialiasing.
    float d = length(localPos) - 0.5 / 1.15;
    vec3 color = mix(instanceTint.rgb, vec3(0.12, 0.15, 0.20), outlineFactor(d));
    FragColor = vec4(color, centeredFillCoverage(d) * instanceTint.a);
}
