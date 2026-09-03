#version 330 core
out vec4 FragColor;
in vec2 localPos; // Coordinates from -0.5 to 0.5

void main()
{
    // A simple math triangle pointing right:
    // 1. The flat back edge: x must be > -0.3
    // 2. The sloped edges: absolute y must be less than a slope based on x
    float backEdge = -localPos.x - 0.3;
    float slopedEdges = abs(localPos.y) - 0.5 * (0.3 - localPos.x);

    // Combining them gives a simple distance metric for a triangle
    float d = max(backEdge, slopedEdges);

    // Anti-aliasing so the edges aren't pixelated
    float aa = fwidth(d);
    float fillFactor = 1.0 - smoothstep(-aa, aa, d);

    // A nice bright Cyan/Teal (No red, no green, distinct from Blue/Orange/Purple)
    vec3 gateColor = vec3(0.1, 0.75, 0.75); 
    
    FragColor = vec4(gateColor, fillFactor);
}