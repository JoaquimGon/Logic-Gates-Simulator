#version 330 core
out vec4 FragColor;
in vec2 localPos;

float sdVesica(vec2 p, float r, float d)
{
    float d1 = length(p - vec2(0.0,  d)) - r;
    float d2 = length(p - vec2(0.0, -d)) - r;
    return max(d1, d2);
}

float sdBox(vec2 p, vec2 b)
{
    vec2 d = abs(p) - b;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

// Shared with the OR gate — same body, no coordinate shift applied here
// (the caller applies it once) so it can be reused cleanly.
float sdOrBody(vec2 p)
{
    float lens = sdVesica(p, 0.75, 0.35);
    float backCircle = length(p - vec2(-1.55, 0.0)) - 1.05;
    return max(lens, -backCircle);
}

float sdXorGate(vec2 p)
{
    p.x -= 0.05;

    float body = sdOrBody(p);

    // XOR's signature second curve: a thin arc riding just behind the body,
    // built as a ring (circle outline) around a slightly-further-left circle.
    float backCircle2 = length(p - vec2(-1.75, 0.0)) - 1.05;
    float lineThickness = 0.045;
    float extraLine = abs(backCircle2) - lineThickness;

    // Clip the arc to roughly the body's vertical extent so it doesn't poke
    // out as a stray ring above/below the gate.
    float clipBox = sdBox(p - vec2(-0.75, 0.0), vec2(0.35, 0.55));
    extraLine = max(extraLine, clipBox);

    return min(body, extraLine); // union: body OR extra line
}

void main()
{
    vec2 p = localPos * 1.3;

    float d = sdXorGate(p);
    float aa = fwidth(d);
    float fillFactor = 1.0 - smoothstep(-aa, aa, d);

    vec3 gateColor = vec3(0.7, 0.3, 0.85); // distinct from AND and OR
    FragColor = vec4(gateColor, fillFactor);
}