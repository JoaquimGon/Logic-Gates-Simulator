#version 330 core

#include "../common/outline.glsl"
#include "../common/sdf.glsl"
out vec4 FragColor;
in vec4 instanceTint;


in vec2 localPos; // Range: -0.5 .. 0.5

/** @brief Shade the concave OR silhouette using the shared antialiased fill and inset outline. */
void main()
{
    vec2 p = localPos;

    float d = sdOrGate(p);

    float fillFactor = fillCoverage(d);

    float outline = outlineFactor(d);
    vec3 color = mix(instanceTint.rgb, vec3(0.12, 0.15, 0.20), outline);
    FragColor = vec4(color, fillFactor * instanceTint.a);
}
