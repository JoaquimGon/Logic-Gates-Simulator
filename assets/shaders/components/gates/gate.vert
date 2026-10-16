#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aInstancePos;

layout(location = 2) in vec2 aInstanceSize;
layout(location = 3) in vec4 aInstanceTint;
layout(location = 4) in float aInstanceOrientation;
out vec4 instanceTint;
out vec2 localPos;


uniform mat4 uViewProjection;


/** @brief Apply the instance size and quarter-turn orientation while retaining local coordinates for silhouette
 * evaluation. */
void main()
{
    localPos = aPos.xy;
    instanceTint = aInstanceTint;

    vec2 offset = aPos.xy * aInstanceSize;
    if (aInstanceOrientation > 2.5)
        offset = vec2(offset.y, -offset.x);
    else if (aInstanceOrientation > 1.5)
        offset = -offset;
    else if (aInstanceOrientation > 0.5)
        offset = vec2(-offset.y, offset.x);
    vec2 worldPos = aInstancePos + offset;

    gl_Position = uViewProjection * vec4(worldPos, 0.0, 1.0);
}
