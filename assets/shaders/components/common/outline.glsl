// Inset rim tapers below normal zoom; screen previews retain the same thickness.
uniform float uOutlineScale = 1.0;

float outlineFactor(float distance)
{
    float pixelWidth = max(length(vec2(dFdx(distance), dFdy(distance))), 0.000001);
    float strokeWidth = 1.5 * uOutlineScale;
    return smoothstep(-(strokeWidth + 0.5) * pixelWidth,
                      -max(strokeWidth - 0.5, 0.0) * pixelWidth, distance);
}

float fillCoverage(float distance)
{
    return 1.0 - smoothstep(0.0, fwidth(distance), distance);
}

float centeredFillCoverage(float distance)
{
    float aa = fwidth(distance);
    return 1.0 - smoothstep(-aa, aa, distance);
}
