#version 330 core

#include "common/sdf.glsl"
#include "common/outline.glsl"
out vec4 FragColor;
in vec4 instanceTint;
in vec2 localPos; // -0.5..0.5, independent of camera (same contract as the gates)

void main()
{
    vec2 p = localPos * 1.3; // same internal shape scale as the gates

    // The body is kept just narrow enough that the output pin â€” which sits one
    // grid cell (0.05 world units) to the right of the component centre â€” lands
    // on the body's right edge instead of floating in the middle of it.
    const float halfSize = 0.42;
    float body = sdRoundBox(p, vec2(halfSize), 0.14);

    // Hollow triangle (dinstict from a gate) 
    float glyph = sdRightTriangle(p, 0.28, 0.32);
    float d = max(body, -glyph);

    float fillFactor = centeredFillCoverage(d);

    // Warm yellow colour
    float outline = outlineFactor(body);
    vec3 color = mix(instanceTint.rgb, vec3(0.12, 0.15, 0.20), outline);
    FragColor = vec4(color, fillFactor * instanceTint.a);
}
