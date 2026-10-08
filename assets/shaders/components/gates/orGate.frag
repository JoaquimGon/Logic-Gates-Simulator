#version 330 core

#include "../common/sdf.glsl"
#include "../common/outline.glsl"
out vec4 FragColor;
in vec4 instanceTint;
in vec2 localPos; // Range: -0.5 .. 0.5

void main()
{
    vec2 p = localPos;

    float d = sdOrGate(p);

    // Anti-aliasing
    float fillFactor = fillCoverage(d);

    float outline = outlineFactor(d);
    vec3 color = mix(instanceTint.rgb, vec3(0.12, 0.15, 0.20), outline);
    FragColor = vec4(color, fillFactor * instanceTint.a);
}