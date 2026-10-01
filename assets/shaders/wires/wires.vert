#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor;
out vec4 vertexColor;
uniform mat4 uViewProjection;
void main()
{
    gl_Position = uViewProjection * vec4(aPos.xy, 0.0, 1.0);
    vertexColor = aColor;
}
