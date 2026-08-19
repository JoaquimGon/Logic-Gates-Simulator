#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor; // NEW: Grab the color from the VBO array!

out vec4 vertexColor; // Pass the color down the pipeline

uniform vec2 uPanOffset;
uniform float uZoom;
uniform float uAspectRatio;

void main()
{
    vec2 pos = (aPos.xy - uPanOffset) * uZoom;
    pos.x /= uAspectRatio;

    gl_Position = vec4(pos, 0.0, 1.0);
    
    // Pass the specific color of this line segment to the fragment shader
    vertexColor = aColor; 
}