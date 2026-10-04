#version 330 core
out vec4 FragColor;
in vec4 instanceTint;
uniform float uOutlineScale = 1.0;
in vec2 localPos; // Range: -0.5 .. 0.5

// AND Gate SDF: Flat back at -halfSize.x, curved nose reaching +halfSize.x
float sdAndGate(vec2 p, vec2 halfSize)
{
    p.y = abs(p.y);

    // Split point where the flat top/bottom ends and the curved cap begins.
    // The curve radius matches the half-height (halfSize.y) so it meets the walls seamlessly.
    float splitX = halfSize.x - halfSize.y;

    // Union the cap with the straight body: their internal join is not an edge.
    float boxHalfW = (splitX + halfSize.x) * 0.5;
    float boxCenterX = -halfSize.x + boxHalfW;
    vec2 q = abs(p - vec2(boxCenterX, 0.0)) - vec2(boxHalfW, halfSize.y);
    float straight = min(max(q.x, q.y), 0.0) + length(max(q, 0.0));
    float cap = length(p - vec2(splitX, 0.0)) - halfSize.y;
    return min(straight, cap);
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
    float aa = fwidth(d);
    float fillFactor = 1.0 - smoothstep(0.0, aa, d);

    float outline = outlineFactor(d);
    // Preserve the bubble's complete rim where it meets the gate body.
    float bubbleCoverage = 1.0 - smoothstep(0.0, max(fwidth(bubble), 0.000001), bubble);
    outline = max(outline, outlineFactor(bubble) * bubbleCoverage);
    vec3 color = mix(instanceTint.rgb, vec3(0.12, 0.15, 0.20), outline);
    FragColor = vec4(color, fillFactor * instanceTint.a);
}