#version 450 core

layout(location = 0) in vec3  aPos;
layout(location = 1) in vec2  aUV;
layout(location = 2) in uint  aTexIndex;

layout(std140, binding = 0) uniform CameraUBO
{
    mat4 MVP;
};

out vec2 vUV;
flat out uint vTexIndex;

void main()
{
    gl_Position = MVP * vec4(aPos, 1.0);
    vUV = aUV;
    vTexIndex = aTexIndex;
}
