#version 450 core
layout(early_fragment_tests) in;

in vec2 vUV;
flat in uint vTexIndex;

layout(binding = 0) uniform sampler2DArray uTextures;

out vec4 FragColor;

void main()
{
    vec2 uv = clamp(vUV, 0.001, 0.999);
    FragColor = texture(uTextures, vec3(uv, vTexIndex));
}
