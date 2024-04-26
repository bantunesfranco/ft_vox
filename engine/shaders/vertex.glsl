#version 330 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color;
out vec3 FragPos;
out vec3 Color;
uniform mat4 tranform;
uniform mat4 view;
uniform mat4 projection;
void main() {
    FragPos = vec3(tranform * vec4(position, 1.0));
    Color = color;
    gl_Position = projection * view * vec4(FragPos, 1.0);
}