#version 330 core
out vec4 FragColor;
in vec4 instanceTint;
uniform float uOutlineScale = 1.0;
in vec2 localPos; // -0.5..0.5, independent of camera (same contract as the gates)

// Rounded box SDF â€” the terminal's body silhouette.
float sdRoundBox(vec2 p, vec2 b, float r)
{
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

// Right-pointing triangle (flat back edge at -x, tip at +x). Same approximate
// metric style notGate.frag uses: cheap, and stable under fwidth().
float sdRightTriangle(vec2 p, float halfW, float halfH)
{
    float backEdge = -p.x - halfW;
    float slopedEdges = abs(p.y) - halfH * (halfW - p.x) / (2.0 * halfW);
    return max(backEdge, slopedEdges);
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
    vec2 p = localPos * 1.3; // same internal shape scale as the gates

    // The body is kept just narrow enough that the output pin â€” which sits one
    // grid cell (0.05 world units) to the right of the component centre â€” lands
    // on the body's right edge instead of floating in the middle of it.
    const float halfSize = 0.42;
    float body = sdRoundBox(p, vec2(halfSize), 0.14);

    // Hollow triangle (dinstict from a gate) 
    float glyph = sdRightTriangle(p, 0.28, 0.32);
    float d = max(body, -glyph);

    float aa = fwidth(d);
    float fillFactor = 1.0 - smoothstep(-aa, aa, d);

    // Warm yellow colour
    float outline = outlineFactor(body);
    vec3 color = mix(instanceTint.rgb, vec3(0.12, 0.15, 0.20), outline);
    FragColor = vec4(color, fillFactor * instanceTint.a);
}
