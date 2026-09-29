#version 330 core
in vec2 TexCoord;
in vec4 TextColor;

out vec4 FragColor;

uniform sampler2D uFontTexture;

void main()
{
    // The atlas holds alpha values in the red channel
    float alpha = texture(uFontTexture, TexCoord).r;
    FragColor = vec4(TextColor.rgb, TextColor.a * alpha);
}