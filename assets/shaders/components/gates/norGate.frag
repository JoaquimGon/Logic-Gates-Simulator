#version 330 core
out vec4 FragColor;
in vec4 instanceTint;
uniform float uOutlineScale = 1.0;
in vec2 localPos; // Range: -0.5 .. 0.5

float sdCircle(vec2 p, vec2 center, float r)
{
    return length(p - center) - r;
}

float sdOrGate(vec2 p)
{
    // Lens body: circles intersect at tip
    float Rc = 1.05;
    float cy = 0.65;
    float cx = -0.334;

    float topCircle = sdCircle(p, vec2(cx,  cy), Rc);
    float botCircle = sdCircle(p, vec2(cx, -cy), Rc);
    float lens = max(topCircle, botCircle);

    // Concave back cut
    vec2 backCenter = vec2(-1.4, 0.0);
    float backR = 0.98;
    float backCut = sdCircle(p, backCenter, backR);

    // Carve back cavity out of the lens body
    return max(lens, -backCut);
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
    vec2 p = vec2(localPos.x * 1.5, localPos.y);

    float d = sdOrGate(p);

    // Invertion circle
    float bubble = length(p - vec2(0.615, 0.0)) - 0.13;
    d = min(d, bubble);
    // Anti-aliasing
    float aa = fwidth(d);
    float fillFactor = 1.0 - smoothstep(0.0, aa, d);

    float outline = outlineFactor(d);
    // Preserve the bubble's complete rim where it meets the gate body.
    float bubbleCoverage = 1.0 - smoothstep(0.0, max(fwidth(bubble), 0.000001), bubble);
    outline = max(outline, outlineFactor(bubble) * bubbleCoverage);
    vec3 color = mix(instanceTint.rgb, vec3(0.12, 0.15, 0.20), outline);
    FragColor = vec4(color, fillFactor * instanceTint.a);
}