#version 330 core
out vec4 FragColor;
in vec4 instanceTint;
in vec2 localPos;

void main()
{
    float d = length(localPos) - 0.32;
    float aa = fwidth(d);
    float fill = 1.0 - smoothstep(0.0, aa, d);
    float rim = smoothstep(-0.035 - aa, -0.035, d);
    vec3 color = mix(instanceTint.rgb, vec3(0.4, 0.45, 0.5), rim);
    vec2 highlight = localPos - vec2(-0.09, 0.10);
    color += instanceTint.rgb * 0.12 * exp(-60.0 * dot(highlight, highlight));
    FragColor = vec4(color, fill * instanceTint.a);
}
