#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aInstancePos;

layout (location = 2) in vec2 aInstanceSize;
layout (location = 3) in vec4 aInstanceTint;
out vec4 instanceTint;
out vec2 localPos;


uniform vec2  uPanOffset;
uniform float uZoom;
uniform float uAspectRatio;

void main()
{
    // Pass the local geometry coordinates (-0.5 to 0.5) to your SDF fragment shader
    localPos = aPos.xy;
    instanceTint = aInstanceTint;

    // Instance math (same as before)
    vec2 worldPos = aInstancePos + (aPos.xy * aInstanceSize);

    vec2 correctedPos = (worldPos - uPanOffset) * uZoom;
    correctedPos.x /= uAspectRatio;

    gl_Position = vec4(correctedPos, 0.0, 1.0);
}
