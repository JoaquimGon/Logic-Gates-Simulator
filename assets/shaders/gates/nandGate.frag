#version 330 core
out vec4 FragColor;
in vec2 localPos;

float sdBox(in vec2 p, in vec2 b) {
    vec2 d = abs(p) - b;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

float sdAndGate(in vec2 p) {
    p.x -= 0.05;
    float halfWidth  = 0.591;
    float halfHeight = 0.591;
    vec2 boxP = p - vec2(-halfWidth, 0.0);
    float box = sdBox(boxP, vec2(halfWidth, halfHeight));
    float cap = (p.x > 0.0) ? length(p) - halfHeight : abs(p.y) - halfHeight;
    return min(box, cap);
}

void main() {
    // Scales X to counteract the wider 0.3 quad so the gate body matches AND exactly
    vec2 p = vec2(localPos.x * 1.95, localPos.y * 1.3);

    float d = sdAndGate(p);
    
    // Solid circle bridging the gate tip (0.64) and the pin (0.97)
    float bubble = length(p - vec2(0.81, 0.0)) - 0.18;
    d = min(d, bubble);

    // Hard clip on the left edge so it doesn't draw past the input pins
    d = max(d, -0.3333 - localPos.x);

    float aa = fwidth(d);
    float fillFactor = 1.0 - smoothstep(-aa, aa, d);

    vec3 gateColor = vec3(0.2, 0.5, 0.9); // AND Blue
    FragColor = vec4(gateColor, fillFactor);
}