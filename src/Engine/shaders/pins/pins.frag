#version 330 core

out vec4 FragColor;

uniform vec4 uColor; // Base pin color (e.g. green for input, red for output)

void main()
{
    // gl_PointCoord ranges from (0.0, 0.0) to (1.0, 1.0) over the point square
    vec2 coord = gl_PointCoord - vec2(0.5);
    float dist = length(coord);

    // Discard fragments outside the circle radius (0.5)
    if (dist > 0.5) {
        discard;
    }

    // Optional: Add a subtle darker border around the pin rim
    float borderThickness = 0.08;
    if (dist > 0.5 - borderThickness) {
        FragColor = vec4(uColor.rgb * 0.4, 1.0); // Darker border
    } else {
        FragColor = uColor;
    }
}