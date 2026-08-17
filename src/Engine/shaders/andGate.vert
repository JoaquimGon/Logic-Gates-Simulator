#version 330 core
layout (location = 0) in vec3 aPos;

out vec2 localPos; // stays in unit-quad space, -0.5..0.5, for the SDF

uniform vec2  uGatePosition;
uniform vec2  uGateSize;
uniform vec2  uPanOffset;
uniform float uZoom;
uniform float uAspectRatio;

void main()
{
    localPos = aPos.xy;

    vec2 worldPos    = uGatePosition + aPos.xy * uGateSize;
    vec2 correctedPos = (worldPos - uPanOffset) * uZoom;

    vec2 ndc;
    ndc.x = correctedPos.x / uAspectRatio;
    ndc.y = correctedPos.y;

    gl_Position = vec4(ndc, 0.0, 1.0);
}