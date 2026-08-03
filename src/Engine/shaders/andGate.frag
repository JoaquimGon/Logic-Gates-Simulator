#version 330 core
out vec4 FragColor;

in vec2 localPos;

uniform vec2  uPanOffset;
uniform float uZoom;

float sdBox(in vec2 p, in vec2 b)
{
    vec2 d = abs(p) - b;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

// A box with a rounded ("D" shaped) right end — this is the whole AND gate.
float sdAndGate(in vec2 p)
{
    p.x -= 0.05;

    float halfWidth  = 0.2955; // 0.591 * 0.5
    float halfHeight = 0.591;

    vec2 boxP = p - vec2(-halfWidth, 0.0);
    float box = sdBox(boxP, vec2(halfWidth, halfHeight));

    float cap = (p.x > 0.0) ? length(p) - halfHeight
                            : abs(p.y) - halfHeight;

    return min(box, cap);
}

void main()
{
    vec2 worldPos = localPos / uZoom + uPanOffset;
    vec2 p = worldPos * 1.3;

    float d = sdAndGate(p);
    float fillFactor = 1.0 - smoothstep(0.0, 0.005 / uZoom, d);

    vec3 gateColor = vec3(0.2, 0.5, 0.9);

    // No background — alpha carries the shape, grid shows through outside it
    FragColor = vec4(gateColor, fillFactor);
}