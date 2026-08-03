#version 330 core
layout (location = 0) in vec3 aPos;

out vec2 localPos; // Pass this to the fragment shader!

void main()
{
    localPos = aPos.xy; // Passes the -0.5 to 0.5 position
    gl_Position = vec4(aPos, 1.0);
}