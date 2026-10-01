#version 330 core
out vec4 FragColor;

in vec2 localPos;

uniform mat4 uInverseViewProjection;
uniform float uGridSpacing;

float gridFactor(vec2 worldPos, float spacing)
{
    vec2 coord = worldPos / spacing;
    vec2 grid  = abs(fract(coord - 0.5) - 0.5) / fwidth(coord);
    return 1.0 - min(min(grid.x, grid.y), 1.0);
}

void main()
{
    vec2 worldPos = (uInverseViewProjection * vec4(localPos, 0.0, 1.0)).xy;

    vec3 bgColor   = vec3(0.10, 0.10, 0.10);
    vec3 gridColor = vec3(0.25, 0.25, 0.25);

    float grid = gridFactor(worldPos, uGridSpacing);
    vec3 finalColor = mix(bgColor, gridColor, grid);

    FragColor = vec4(finalColor, 1.0);
}