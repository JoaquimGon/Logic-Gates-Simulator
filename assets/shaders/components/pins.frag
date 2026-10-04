#version 330 core

uniform float uOutlineScale = 1.0;
in vec4 vertexColor;
out vec4 FragColor;

void main()
{
    // Local coordinates: center is (0.0, 0.0), circular border sits at radius 0.5
    vec2 coord = gl_PointCoord - vec2(0.5);
    float dist = length(coord);

    // Exact derivative width corresponding to 1 screen pixel
    float delta = fwidth(dist);

    const float outerR = 0.48;
    // Match component outlines in pixel width; retain a visible core on tiny points.
    float pixelWidth = max(length(vec2(dFdx(dist), dFdy(dist))), 0.000001);
    float innerR = outerR - min(1.5 * uOutlineScale * pixelWidth, 0.18);

    // Hard, crisp AA for the inner core -> dark border transition
    float rimFactor = smoothstep(innerR - delta * 0.5, innerR + delta * 0.5, dist);

    // Slate border matching the output bulb's unpowered fill
    vec4 borderColor = vec4(0.12, 0.15, 0.20, vertexColor.a);
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