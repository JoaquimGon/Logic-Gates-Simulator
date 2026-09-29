#version 330 core
out vec4 FragColor;
in vec2 localPos; // Range: -0.5 .. 0.5

// AND Gate SDF: Flat back at -halfSize.x, curved nose reaching +halfSize.x
float sdAndGate(vec2 p, vec2 halfSize)
{
    p.y = abs(p.y);

    // Split point where the flat top/bottom ends and the curved cap begins.
    // The curve radius matches the half-height (halfSize.y) so it meets the walls seamlessly.
    float splitX = halfSize.x - halfSize.y;

    if (p.x > splitX) {
        // Semi-circle arc centered at (splitX, 0) reaching exactly +halfSize.x at its tip
        return length(p - vec2(splitX, 0.0)) - halfSize.y;
    }

    // Straight body box spanning from -halfSize.x to splitX
    float boxHalfW = (splitX - (-halfSize.x)) * 0.5;
    float boxCenterX = -halfSize.x + boxHalfW;

    vec2 d = abs(p - vec2(boxCenterX, 0.0)) - vec2(boxHalfW, halfSize.y);
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
}

void main()
{
    // Local quad bounds:
    // X goes from -0.5 (inputs) to +0.5 (output pin).
    // Y has a small margin so the top and bottom edges anti-alias cleanly.
    const float halfWidth  = 0.5;
    const float halfHeight = 0.42;

    vec2 p = vec2(localPos.x * 1.5, localPos.y);

    float d = sdAndGate(p, vec2(halfWidth, halfHeight));

    // Invertion circle
    float bubble = length(p - vec2(0.615, 0.0)) - 0.13;
    d = min(d, bubble);

    // Pixel-width anti-aliasing
    float aa = fwidth(d);
    float fillFactor = 1.0 - smoothstep(0.0, aa, d);

    vec3 gateColor = vec3(0.2, 0.5, 0.9);
    FragColor = vec4(gateColor, fillFactor);
}