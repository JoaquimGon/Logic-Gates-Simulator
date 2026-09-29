#version 330 core
out vec4 FragColor;
in vec2 localPos; // Range: -0.5 .. 0.5

float sdCircle(vec2 p, vec2 center, float r)
{
    return length(p - center) - r;
}

float sdXorGate(vec2 p)
{
    // Lens body: circles intersect at tip (+0.49, 0.0)
    float Rc = 1.05;
    float cy = 0.65;
    float cx = -0.334; 

    float topCircle = sdCircle(p, vec2(cx,  cy), Rc);
    float botCircle = sdCircle(p, vec2(cx, -cy), Rc);
    float lens = max(topCircle, botCircle);

    // Concave back cut
    vec2 backCenter = vec2(-1.27, 0.0);
    float backR = 0.98; // Apex sits at -1.20 + 0.98 = -0.22
    float backCut = sdCircle(p, backCenter, backR);
    float body = max(lens, -backCut);

    // Trailing input arc
    float arcR = 0.860;
    float arcCircle = sdCircle(p, backCenter, arcR);
    float arcLine = abs(arcCircle) - 0.013;
    float arcVertical = abs(p.y) - 0.38;
    float trailingArc = max(arcLine, arcVertical);

    return min(body, trailingArc);
}

void main()
{
    vec2 p = localPos;

    float d = sdXorGate(p);

    // Anti-aliasing
    float aa = fwidth(d);
    float fillFactor = 1.0 - smoothstep(0.0, aa, d);

    vec3 gateColor = vec3(0.7, 0.3, 0.85); // XOR Purple
    FragColor = vec4(gateColor, fillFactor);
}