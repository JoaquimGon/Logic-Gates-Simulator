#version 330 core
out vec4 FragColor;
in vec2 localPos; // -0.5..0.5, independent of camera now

float sdBox(in vec2 p, in vec2 b)
{
    vec2 d = abs(p) - b;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

float sdAndGate(in vec2 p)
{
    p.x -= 0.05;
    float halfWidth  = 0.591;
    float halfHeight = 0.591;

    vec2 boxP = p - vec2(-halfWidth, 0.0);
    float box = sdBox(boxP, vec2(halfWidth, halfHeight));

    float cap = (p.x > 0.0) ? length(p) - halfHeight
                             : abs(p.y) - halfHeight;
    return min(box, cap);
}

void main()
{
    vec2 p = localPos * 1.3; // same internal shape scale as before

    float d = sdAndGate(p);
    float aa = fwidth(d); // screen-derivative AA, adapts to zoom automatically
    float fillFactor = 1.0 - smoothstep(-aa, aa, d);

    vec3 gateColor = vec3(0.2, 0.5, 0.9);
    FragColor = vec4(gateColor, fillFactor);
}