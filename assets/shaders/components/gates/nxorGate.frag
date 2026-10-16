#version 330 core

#include "../common/outline.glsl"
#include "../common/sdf.glsl"
out vec4 FragColor;
in vec4 instanceTint;
uniform bool uDrawRearArc = true;


in vec2 localPos; // Range: -0.5 .. 0.5

/** @brief Shade the XOR body, optional rear arc and output bubble while retaining each visible rim. */
void main()
{
    // Reserve room for the output inversion bubble within the shared shader quad.
    vec2 p = vec2(localPos.x * 1.5, localPos.y);

    float body = sdXorGate(p);
    if (!uDrawRearArc)
    {
        float d = body;
        float outline = outlineFactor(body);
        float bubble = length(p - vec2(0.615, 0.0)) - 0.13;
        d = min(body, bubble);
        outline =
            max(outlineFactor(d),
                outlineFactor(bubble) * (1.0 - smoothstep(0.0, max(fwidth(bubble), 0.000001), bubble)));
        FragColor = vec4(mix(instanceTint.rgb, vec3(0.12, 0.15, 0.20), outline), fillCoverage(d) * instanceTint.a);
        return;
    }
    float arcCircle = sdCircle(p, vec2(-1.27, 0.0), 0.860);
    float trailingArc = max(abs(arcCircle) - 0.013, abs(p.y) - 0.38);
    // Use the circle gradient: abs() loses a stable derivative along the arc center.
    vec2 arcGradient = vec2(dFdx(arcCircle), dFdy(arcCircle));
    vec2 capGradient = vec2(dFdx(p.y), dFdy(p.y));
    float arcPixelWidth = max(length(abs(arcCircle) - 0.013 > abs(p.y) - 0.38 ? arcGradient : capGradient), 0.000001);
    // Keep the colored arc's width; add its rim outward and let the quad clip it.
    float borderedArc = trailingArc - 1.5 * uOutlineScale * arcPixelWidth;
    float d = min(body, borderedArc);

    float bubble = length(p - vec2(0.615, 0.0)) - 0.13;
    d = min(d, bubble);

    float fillFactor = fillCoverage(d);

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
