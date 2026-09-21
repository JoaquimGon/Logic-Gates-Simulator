#version 330 core
out vec4 FragColor;
in vec2 localPos;

float sdVesica(vec2 p, float r, float d) {
    float d1 = length(p - vec2(0.0,  d)) - r;
    float d2 = length(p - vec2(0.0, -d)) - r;
    return max(d1, d2);
}

float sdBox(vec2 p, vec2 b) {
    vec2 d = abs(p) - b;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

float sdOrBody(vec2 p) {
    float lens = sdVesica(p, 0.75, 0.35);
    float backCircle = length(p - vec2(-1.55, 0.0)) - 1.05;
    return max(lens, -backCircle);
}

float sdXorGate(vec2 p) {
    p.x -= 0.05;
    float body = sdOrBody(p);
    float backCircle2 = length(p - vec2(-1.75, 0.0)) - 1.05;
    float extraLine = abs(backCircle2) - 0.045;
    float clipBox = sdBox(p - vec2(-0.75, 0.0), vec2(0.35, 0.55));
    extraLine = max(extraLine, clipBox);
    return min(body, extraLine);
}

void main() {
    vec2 p = vec2(localPos.x * 1.95, localPos.y * 1.3);

    float d = sdXorGate(p);
    
    float bubble = length(p - vec2(0.84, 0.0)) - 0.16;
    d = min(d, bubble);

    d = max(d, -0.3333 - localPos.x);

    float aa = fwidth(d);
    float fillFactor = 1.0 - smoothstep(-aa, aa, d);

    vec3 gateColor = vec3(0.7, 0.3, 0.85); // XOR Purple
    FragColor = vec4(gateColor, fillFactor);
}