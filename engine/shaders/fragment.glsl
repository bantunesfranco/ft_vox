#version 330 core

// Inputs from the vertex shader
in vec3 color;       // Color input from vertex shader
in vec2 texCoord;   // Texture coordinates input from vertex shader

// Texture sampler
uniform sampler2D textureSampler;

// Output color
out vec4 fragment;

void main()
{
    // Sample the texture color
    vec4 texColor = texture(textureSampler, texCoord);
    
    // Combine texture color with vertex color (modulate)
    fragment = vec4(color * texColor.rgb, texColor.a);
}
