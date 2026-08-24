#version 330 core

in vec4 vertexColor; // Received from the vertex shader
out vec4 FragColor;

void main()
{
    // Optional: This little math trick turns your GL_POINTS from squares into perfect circles!
    vec2 coord = gl_PointCoord - vec2(0.5);
    if (length(coord) > 0.5) {
        discard;
    }

    // Paint the pixel with the instance's unique color
    FragColor = vertexColor;
}