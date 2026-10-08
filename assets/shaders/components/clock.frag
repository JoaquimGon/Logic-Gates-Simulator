#version 330 core

#include "common/sdf.glsl"
#include "common/outline.glsl"
out vec4 FragColor;
in vec4 instanceTint;
in vec2 localPos; // -0.5 .. 0.5

void main()
{
    vec2 p = localPos * 1.3; // Match gate internal scale

    // 1. Outer rounded square body
    const float halfSize = 0.42;
    float body = sdBox(p, vec2(halfSize)) - 0.05;

    // 2. Square-wave clock symbol inside: _/â€¾\_
    float w1 = sdSegment(p, vec2(-0.25, -0.15), vec2(-0.10, -0.15));
    float w2 = sdSegment(p, vec2(-0.10, -0.15), vec2(-0.10,  0.15));
    float w3 = sdSegment(p, vec2(-0.10,  0.15), vec2( 0.10,  0.15));
    float w4 = sdSegment(p, vec2( 0.10,  0.15), vec2( 0.10, -0.15));
    float w5 = sdSegment(p, vec2( 0.10, -0.15), vec2( 0.25, -0.15));
    
    float glyph = min(min(min(min(w1, w2), w3), w4), w5) - 0.035;

    float d = max(body, -glyph);

    float fillFactor = fillCoverage(d);

    // Slate / Dark cyan tone for clocks
    float outline = outlineFactor(body);
    vec3 color = mix(instanceTint.rgb, vec3(0.12, 0.15, 0.20), outline);
    FragColor = vec4(color, fillFactor * instanceTint.a);
}