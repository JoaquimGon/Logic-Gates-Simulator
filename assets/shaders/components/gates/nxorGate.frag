#version 330 core
out vec4 FragColor;
in vec4 instanceTint;
uniform float uOutlineScale = 1.0;
in vec2 localPos; // Range: -0.5 .. 0.5

float sdCircle(vec2 p, vec2 center, float r)
{
    return length(p - center) - r;
}

float sdXorGate(vec2 p)
{
    // Lens body: circles intersect at tip (+0.49, 0.0)
    float Rc = 1.05;
    float cy = 0.65;
    float cx = -0.334; 

    float topCircle = sdCircle(p, vec2(cx,  cy), Rc);
    float botCircle = sdCircle(p, vec2(cx, -cy), Rc);
    float lens = max(topCircle, botCircle);

    // Concave back cut
    vec2 backCenter = vec2(-1.27, 0.0);
    float backR = 0.98; // Apex sits at -1.20 + 0.98 = -0.22
    float backCut = sdCircle(p, backCenter, backR);
    float body = max(lens, -backCut);

    return body;
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
    // Decrease size due to change in size to account for the
    // convertion circle
    vec2 p = vec2(localPos.x * 1.5, localPos.y);

    float body = sdXorGate(p);
    float arcCircle = sdCircle(p, vec2(-1.27, 0.0), 0.860);
    float trailingArc = max(abs(arcCircle) - 0.013, abs(p.y) - 0.38);
    // Use the circle gradient: abs() loses a stable derivative along the arc center.
    vec2 arcGradient = vec2(dFdx(arcCircle), dFdy(arcCircle));
    vec2 capGradient = vec2(dFdx(p.y), dFdy(p.y));
    float arcPixelWidth = max(length(abs(arcCircle) - 0.013 > abs(p.y) - 0.38
                                    ? arcGradient : capGradient), 0.000001);
    // Keep the colored arc's width; add its rim outward and let the quad clip it.
    float borderedArc = trailingArc - 1.5 * uOutlineScale * arcPixelWidth;
    float d = min(body, borderedArc);

    // Invertion circle
    float bubble = length(p - vec2(0.615, 0.0)) - 0.13;
    d = min(d, bubble);

    // Anti-aliasing
    float aa = fwidth(d);
    float fillFactor = 1.0 - smoothstep(0.0, aa, d);

    float outline = outlineFactor(d);
    float arcCoverage = 1.0 - smoothstep(0.0, arcPixelWidth, trailingArc);
    float arcBorderCoverage = 1.0 - smoothstep(0.0, arcPixelWidth, borderedArc);
    // Preserve the bubble's complete rim where it meets the gate body.
    float bubbleCoverage = 1.0 - smoothstep(0.0, max(fwidth(bubble), 0.000001), bubble);
    outline = max(outline, outlineFactor(bubble) * bubbleCoverage);
    vec3 color = mix(instanceTint.rgb, vec3(0.12, 0.15, 0.20), outline);
    color = mix(color, vec3(0.12, 0.15, 0.20), arcBorderCoverage);
    color = mix(color, instanceTint.rgb, arcCoverage);
    FragColor = vec4(color, fillFactor * instanceTint.a);
}