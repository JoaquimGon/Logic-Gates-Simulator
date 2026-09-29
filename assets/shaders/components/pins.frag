#version 330 core

in vec4 vertexColor;
out vec4 FragColor;

void main()
{
    // Local coordinates: center is (0.0, 0.0), circular border sits at radius 0.5
    vec2 coord = gl_PointCoord - vec2(0.5);
    float dist = length(coord);

    // Exact derivative width corresponding to 1 screen pixel
    float delta = fwidth(dist);

    // Core color boundary (radius 0.36) and outer rim boundary (radius 0.48)
    const float innerR = 0.36;
    const float outerR = 0.48;

    // Hard, crisp AA for the inner core -> dark border transition
    float rimFactor = smoothstep(innerR - delta * 0.5, innerR + delta * 0.5, dist);

    // Dark high-contrast border matching dark UI backdrop
    vec4 borderColor = vec4(0.08, 0.08, 0.08, vertexColor.a);
    vec4 col = mix(vertexColor, borderColor, rimFactor);

    // Sub-pixel alpha falloff at the outer rim (analytical anti-aliasing)
    float alpha = 1.0 - smoothstep(outerR - delta, outerR + delta * 0.5, dist);

    // Discard pixels strictly beyond the anti-aliased edge to save blend ops
    if (alpha <= 0.001) {
        discard;
    }

    col.a *= alpha;
    FragColor = col;
}