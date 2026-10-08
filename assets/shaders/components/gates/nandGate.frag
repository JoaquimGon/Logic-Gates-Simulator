#version 330 core

#include "../common/sdf.glsl"
#include "../common/outline.glsl"
out vec4 FragColor;
in vec4 instanceTint;
in vec2 localPos; // Range: -0.5 .. 0.5

void main()
{
    // Local quad bounds:
    // X goes from -0.5 (inputs) to +0.5 (output pin).
    // Y has a small margin so the top and bottom edges anti-alias cleanly.
    const float halfWidth  = 0.5;
    const float halfHeight = 0.42;

    vec2 p = vec2(localPos.x * 1.5, localPos.y);

    float d = sdAndGate(p, vec2(halfWidth, halfHeight));

    // Invertion circle
    float bubble = length(p - vec2(0.615, 0.0)) - 0.13;
    d = min(d, bubble);

    // Pixel-width anti-aliasing
    float fillFactor = fillCoverage(d);

    float outline = outlineFactor(d);
    // Preserve the bubble's complete rim where it meets the gate body.
    float bubbleCoverage = 1.0 - smoothstep(0.0, max(fwidth(bubble), 0.000001), bubble);
    outline = max(outline, outlineFactor(bubble) * bubbleCoverage);
    vec3 color = mix(instanceTint.rgb, vec3(0.12, 0.15, 0.20), outline);
    FragColor = vec4(color, fillFactor * instanceTint.a);
}