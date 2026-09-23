#version 330 core
out vec4 FragColor;
in vec2 localPos; // Range: -0.5 .. 0.5

float sdCircle(vec2 p, vec2 center, float r)
{
    return length(p - center) - r;
}

float sdOrGate(vec2 p)
{
    // Lens body: circles intersect at tip
    float Rc = 1.05;
    float cy = 0.65;
    float cx = -0.334;

    float topCircle = sdCircle(p, vec2(cx,  cy), Rc);
    float botCircle = sdCircle(p, vec2(cx, -cy), Rc);
    float lens = max(topCircle, botCircle);

    // Concave back cut
    vec2 backCenter = vec2(-1.4, 0.0);
    float backR = 0.98;
    float backCut = sdCircle(p, backCenter, backR);

    // Carve back cavity out of the lens body
    return max(lens, -backCut);
}

void main()
{
    vec2 p = localPos;

    float d = sdOrGate(p);

    // Anti-aliasing
    float aa = fwidth(d);
    float fillFactor = 1.0 - smoothstep(0.0, aa, d);

    vec3 gateColor = vec3(0.9, 0.55, 0.2); // OR Orange
    FragColor = vec4(gateColor, fillFactor);
}