#version 330 core

out vec4 FragColor;

in vec4 vertexColor; // NEW: Receive the color from the vertex shader!

void main()
{
    // Paint the wire using the batched color
    FragColor = vertexColor;
}