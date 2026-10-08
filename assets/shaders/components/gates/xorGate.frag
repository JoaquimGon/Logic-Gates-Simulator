#version 330 core

#include "../common/sdf.glsl"
#include "../common/outline.glsl"
out vec4 FragColor;
in vec4 instanceTint;
in vec2 localPos; // Range: -0.5 .. 0.5

void main()
{
    vec2 p = localPos;

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

    // Anti-aliasing
    float fillFactor = fillCoverage(d);

    float outline = outlineFactor(d);
    float arcCoverage = 1.0 - smoothstep(0.0, arcPixelWidth, trailingArc);
    float arcBorderCoverage = 1.0 - smoothstep(0.0, arcPixelWidth, borderedArc);
    vec3 color = mix(instanceTint.rgb, vec3(0.12, 0.15, 0.20), outline);
    color = mix(color, vec3(0.12, 0.15, 0.20), arcBorderCoverage);
    color = mix(color, instanceTint.rgb, arcCoverage);
    FragColor = vec4(color, fillFactor * instanceTint.a);
}