#version 330 core

in vec3 ourColor;
in vec2 TexCoord;

out vec4 FragColor;

uniform sampler2D ourTexture;
uniform bool useTexture;

void main()
{
    if (useTexture) {
        FragColor = texture(ourTexture, TexCoord);
    } else {
        FragColor = vec4(ourColor, 1.0);
    }
}