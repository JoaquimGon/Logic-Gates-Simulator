#version 330 core

#include "../common/sdf.glsl"
#include "../common/outline.glsl"
out vec4 FragColor;
in vec4 instanceTint;
in vec2 localPos; // Range: -0.5 .. 0.5

void main()
{
    vec2 p = vec2(localPos.x * 1.5, localPos.y);

    float d = sdOrGate(p);

    // Invertion circle
    float bubble = length(p - vec2(0.615, 0.0)) - 0.13;
    d = min(d, bubble);
    // Anti-aliasing
    float fillFactor = fillCoverage(d);

    float outline = outlineFactor(d);
    // Preserve the bubble's complete rim where it meets the gate body.
    float bubbleCoverage = 1.0 - smoothstep(0.0, max(fwidth(bubble), 0.000001), bubble);
    outline = max(outline, outlineFactor(bubble) * bubbleCoverage);
    vec3 color = mix(instanceTint.rgb, vec3(0.12, 0.15, 0.20), outline);
    FragColor = vec4(color, fillFactor * instanceTint.a);
}