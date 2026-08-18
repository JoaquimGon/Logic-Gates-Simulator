#version 330 core

layout (location = 0) in vec3 aPos;

uniform vec2 uPosition;     // Absolute world position of the pin
uniform vec2 uPanOffset;    // Camera pan offset
uniform float uZoom;        // Camera zoom factor
uniform float uAspectRatio; // Framebuffer width / height
uniform float uPointSize;   // Point diameter in screen pixels

void main()
{
    // MUST SUBTRACT uPanOffset to match the gate shader
    vec2 pos = (uPosition - uPanOffset) * uZoom;

    // Aspect Ratio correction
    pos.x /= uAspectRatio;

    gl_Position = vec4(pos, 0.0, 1.0);
    gl_PointSize = uPointSize;  
}