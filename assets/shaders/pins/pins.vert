#version 330 core

layout (location = 0) in vec3 aPos;           // Template point (0,0)
layout (location = 1) in vec2 aInstancePos;   // Unique pin position
layout (location = 2) in vec4 aInstanceColor; // Color (RGBA)

out vec4 vertexColor;

uniform vec2  uPanOffset;
uniform float uZoom;
uniform float uAspectRatio;
uniform float uPointSize;    // World-space diameter (e.g. 0.025 world units)
uniform float uWindowHeight; // Screen viewport height in pixels

void main()
{
    vec2 worldPos = aPos.xy + aInstancePos;
    
    vec2 pos = (worldPos - uPanOffset) * uZoom;
    pos.x /= uAspectRatio;

    gl_Position = vec4(pos, 0.0, 1.0);
    
    // Convert world-space diameter into pixels across the viewport:
    // (uPointSize * uZoom) is NDC height. Multiply by (uWindowHeight * 0.5) to get pixels.
    gl_PointSize = uPointSize * uZoom * (uWindowHeight * 0.5);
    
    vertexColor = aInstanceColor;
}