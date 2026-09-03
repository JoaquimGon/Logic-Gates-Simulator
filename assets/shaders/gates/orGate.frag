#version 330 core
out vec4 FragColor;
in vec2 localPos; // -0.5..0.5, independent of camera

// Intersection of two circles (mirrored across the x-axis) gives a lens
// pointed on both the left and right — this is the OR gate's basic silhouette
// before the back gets carved concave.
float sdVesica(vec2 p, float r, float d)
{
    float d1 = length(p - vec2(0.0,  d)) - r;
    float d2 = length(p - vec2(0.0, -d)) - r;
    return max(d1, d2); // intersection
}

float sdOrGate(vec2 p)
{
    p.x -= 0.05;

    float lens = sdVesica(p, 0.75, 0.35);

    // A large circle sitting outside to the left. Subtracting it eats into
    // the lens's left tip, leaving a concave curve — the OR gate's back edge.
    float backCircle = length(p - vec2(-1.55, 0.0)) - 1.05;

    return max(lens, -backCircle); // subtraction
}

void main()
{
    vec2 p = localPos * 1.3; // same internal shape scale as the AND gate

    float d = sdOrGate(p);
    float aa = fwidth(d);
    float fillFactor = 1.0 - smoothstep(-aa, aa, d);

    vec3 gateColor = vec3(0.9, 0.55, 0.2); // distinct from AND's blue
    FragColor = vec4(gateColor, fillFactor);
}