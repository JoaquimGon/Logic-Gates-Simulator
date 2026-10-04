#version 330 core
out vec4 FragColor;
in vec4 instanceTint;
uniform float uOutlineScale = 1.0;
in vec2 localPos; // -0.5 .. 0.5

// Box distance field
float sdBox(vec2 p, vec2 b)
{
    vec2 d = abs(p) - b;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

// Line segment distance field
float sdSegment(vec2 p, vec2 a, vec2 b)
{
    vec2 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}

// Taper the inset outline below normal zoom; cap it at 1.5 framebuffer pixels.
float outlineFactor(float distance)
{
    float pixelWidth = max(length(vec2(dFdx(distance), dFdy(distance))), 0.000001);
    float strokeWidth = 1.5 * uOutlineScale;
    return smoothstep(-(strokeWidth + 0.5) * pixelWidth,
                      -max(strokeWidth - 0.5, 0.0) * pixelWidth, distance);
}

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

    float aa = fwidth(d);
    float fillFactor = 1.0 - smoothstep(0.0, aa, d);

    // Slate / Dark cyan tone for clocks
    float outline = outlineFactor(body);
    vec3 color = mix(instanceTint.rgb, vec3(0.12, 0.15, 0.20), outline);
    FragColor = vec4(color, fillFactor * instanceTint.a);
}