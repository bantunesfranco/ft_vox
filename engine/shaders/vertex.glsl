#version 330 core

// Uniform for the MVP matrix
uniform mat4 MVP;

// Input attributes
in vec3 vPos;       // Vertex position
in vec3 vCol;       // Vertex color
in vec2 vTexCoord;  // Vertex texture coordinates

// Outputs to the fragment shader
out vec3 color;     // Color output
out vec2 texCoord;  // Texture coordinates output

void main()
{
    gl_Position = MVP * vec4(vPos, 1.0);
    color = vCol;       // Pass color to the fragment shader
    texCoord = vTexCoord; // Pass texture coordinates to the fragment shader
}
