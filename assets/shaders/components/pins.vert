#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aInstancePos;
layout (location = 2) in vec4 aInstanceColor;
out vec4 vertexColor;
uniform mat4 uViewProjection;
uniform float uPointSize; // World-space diameter.
uniform float uPixelsPerWorldUnit; // Uses the actual framebuffer viewport height.
void main()
{
    gl_Position = uViewProjection * vec4(aPos.xy + aInstancePos, 0.0, 1.0);
    gl_PointSize = uPointSize * uPixelsPerWorldUnit;
    vertexColor = aInstanceColor;
}
