#version 330 core
out vec4 FragColor;

in vec2 localPos;

uniform vec2  uPanOffset;
uniform float uZoom;
uniform float uGridSpacing;
uniform vec2  uResolution; // Screen resolution (width, height)

float gridFactor(vec2 worldPos, float spacing)
{
    vec2 coord = worldPos / spacing;
    vec2 grid  = abs(fract(coord - 0.5) - 0.5) / fwidth(coord);
    return 1.0 - min(min(grid.x, grid.y), 1.0);
}

void main()
{
    // Fix aspect ratio so grid cells stay square on rectangular windows
    float aspectRatio = uResolution.x / uResolution.y;
    vec2 correctedPos = localPos;
    correctedPos.x *= aspectRatio;

    // Apply zoom and pan using the corrected aspect ratio position
    vec2 worldPos = correctedPos / uZoom + uPanOffset;

    vec3 bgColor   = vec3(0.10, 0.10, 0.10);
    vec3 gridColor = vec3(0.25, 0.25, 0.25);

    float grid = gridFactor(worldPos, uGridSpacing);
    vec3 finalColor = mix(bgColor, gridColor, grid);

    FragColor = vec4(finalColor, 1.0);
}