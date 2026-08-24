#version 330 core

layout (location = 0) in vec3 aPos;         
layout (location = 1) in vec2 aInstancePos; 

out vec2 localPos; // <--- BRINGING THIS BACK!

uniform vec2  uGateSize; 
uniform vec2  uPanOffset;
uniform float uZoom;
uniform float uAspectRatio;

void main()
{
    // Pass the local geometry coordinates (-0.5 to 0.5) to your SDF fragment shader
    localPos = aPos.xy;

    // Instance math (same as before)
    vec2 worldPos = aInstancePos + (aPos.xy * uGateSize);
    
    vec2 correctedPos = (worldPos - uPanOffset) * uZoom;
    correctedPos.x /= uAspectRatio;

    gl_Position = vec4(correctedPos, 0.0, 1.0);
}