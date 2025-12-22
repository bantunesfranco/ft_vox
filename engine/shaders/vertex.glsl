#version 450 core

layout(location = 0) in vec3 vPos;
layout(location = 1) in vec2 vTexCoord;

uniform mat4 MVP;

out vec2 TexCoords;

void main() {
    gl_Position = MVP * vec4(vPos, 1.0);
    TexCoords = vTexCoord;
}
