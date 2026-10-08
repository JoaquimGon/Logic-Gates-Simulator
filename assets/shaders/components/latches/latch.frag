#version 330 core

#include "../common/sdf.glsl"
#include "../common/outline.glsl"
out vec4 FragColor;
in vec4 instanceTint;
in vec2 localPos;

void main()
{
    // localPos is -0.5 to 0.5. The half extents are equal on both axes and sit
    // just inside the quad, so the body fills the component's 6 x 4 cell footprint
    // while the outermost pixels stay available for the anti-aliased edge.
    vec2 halfSize = vec2(0.48, 0.48);
    float d = sdRoundBox(localPos, halfSize, 0.05);

    float fillFactor = fillCoverage(d);

    // Deep slate blue/charcoal body
    vec3 bodyColor = vec3(0.14, 0.16, 0.22);
    
    // Subtle border
    float border = outlineFactor(d);
    vec3 borderColor = vec3(0.12, 0.15, 0.20);
    vec3 finalColor = mix(bodyColor * instanceTint.rgb, borderColor, border);

    FragColor = vec4(finalColor, fillFactor * instanceTint.a);
}