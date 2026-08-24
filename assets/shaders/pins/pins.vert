#version 330 core

layout (location = 0) in vec3 aPos;           // Template point (0,0)
layout (location = 1) in vec2 aInstancePos;   // NEW: The unique position of THIS pin
layout (location = 2) in vec4 aInstanceColor; // NEW: The unique color of THIS pin (Red, Green, or Blue)

out vec4 vertexColor; // Pass the color to the fragment shader

uniform vec2  uPanOffset;
uniform float uZoom;
uniform float uAspectRatio;
uniform float uPointSize;

void main()
{
    // Points are just (0,0), so we just add the instance position
    vec2 worldPos = aPos.xy + aInstancePos;
    
    // Standard camera math
    vec2 pos = (worldPos - uPanOffset) * uZoom;
    pos.x /= uAspectRatio;

    gl_Position = vec4(pos, 0.0, 1.0);
    gl_PointSize = uPointSize;
    
    // Pass the color straight down the pipeline
    vertexColor = aInstanceColor;
}