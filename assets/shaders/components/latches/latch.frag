#version 330 core
out vec4 FragColor;
in vec4 instanceTint;
uniform float uOutlineScale = 1.0;
in vec2 localPos;

float sdRoundBox(vec2 p, vec2 b, float r)
{
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
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
    // localPos is -0.5 to 0.5. The half extents are equal on both axes and sit
    // just inside the quad, so the body fills the component's 6 x 4 cell footprint
    // while the outermost pixels stay available for the anti-aliased edge.
    vec2 halfSize = vec2(0.48, 0.48);
    float d = sdRoundBox(localPos, halfSize, 0.05);

    float aa = fwidth(d);
    float fillFactor = 1.0 - smoothstep(0.0, aa, d);

    // Deep slate blue/charcoal body
    vec3 bodyColor = vec3(0.14, 0.16, 0.22);
    
    // Subtle border
    float border = outlineFactor(d);
    vec3 borderColor = vec3(0.12, 0.15, 0.20);
    vec3 finalColor = mix(bodyColor * instanceTint.rgb, borderColor, border);

    FragColor = vec4(finalColor, fillFactor * instanceTint.a);
}