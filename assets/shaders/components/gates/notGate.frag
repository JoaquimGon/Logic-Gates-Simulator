#version 330 core
out vec4 FragColor;
in vec2 localPos;

void main()
{
    // The quad is 0.2 wide (4 cells) and 0.10 tall (2 cells) -> 2:1 ratio.
    // Scale Y by 0.5 to keep the coordinate space uniform and the bubble round.
    vec2 p = vec2(localPos.x, localPos.y * 0.5);

    // Flat back edge perfectly aligns with the left input pin at -2 cells (localPos.x = -0.5)
    float backEdge = -p.x - 0.5;
    
    // The sloped edges converge to a tip at the center (localPos.x = 0.0)
    // This creates a triangle exactly 2 grid cells wide.
    float slopedEdges = abs(p.y) - 0.5 * (-p.x);
    float triangle = max(backEdge, slopedEdges);

    // Solid inversion bubble
    // Centered at 0.125 (+0.5 cells) with a radius of 0.125.
    // Diameter is exactly 0.25 local space (1 full grid cell).
    // It bridges the triangle tip (0.0) and ends perfectly at the output pin (0.25).
    float bubble = length(p - vec2(0.125, 0.0)) - 0.125;

    // Combine them
    float d = min(triangle, bubble);

    // Anti-aliasing
    float aa = fwidth(d);
    float fillFactor = 1.0 - smoothstep(-aa, aa, d);

    vec3 gateColor = vec3(0.1, 0.75, 0.75); // Cyan
    FragColor = vec4(gateColor, fillFactor);
}