#version 330 core
out vec4 FragColor;
in vec4 instanceTint;
in vec2 localPos;

float sdRoundBox(vec2 p, vec2 b, float r)
{
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
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
    float border = smoothstep(-0.02 - aa, -0.02, d);
    vec3 borderColor = vec3(0.35, 0.40, 0.50);
    vec3 finalColor = mix(bodyColor, borderColor, border);

    FragColor = vec4(finalColor * instanceTint.rgb, fillFactor * instanceTint.a);
}