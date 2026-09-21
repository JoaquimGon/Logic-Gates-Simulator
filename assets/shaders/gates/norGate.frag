#version 330 core
out vec4 FragColor;
in vec2 localPos;

float sdVesica(vec2 p, float r, float d) {
    float d1 = length(p - vec2(0.0,  d)) - r;
    float d2 = length(p - vec2(0.0, -d)) - r;
    return max(d1, d2);
}

float sdOrGate(vec2 p) {
    p.x -= 0.05;
    float lens = sdVesica(p, 0.75, 0.35);
    float backCircle = length(p - vec2(-1.55, 0.0)) - 1.05;
    return max(lens, -backCircle);
}

void main() {
    vec2 p = vec2(localPos.x * 1.95, localPos.y * 1.3);

    float d = sdOrGate(p);
    
    // Solid circle bridging the OR tip (0.70) and the pin (0.97)
    float bubble = length(p - vec2(0.84, 0.0)) - 0.16;
    d = min(d, bubble);

    d = max(d, -0.3333 - localPos.x);

    float aa = fwidth(d);
    float fillFactor = 1.0 - smoothstep(-aa, aa, d);

    vec3 gateColor = vec3(0.9, 0.55, 0.2); // OR Orange
    FragColor = vec4(gateColor, fillFactor);
}