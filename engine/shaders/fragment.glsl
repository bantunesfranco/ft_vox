#version 450 core
//layout(early_fragment_tests) in;

in vec2 vUV;
in vec3 vNormal;
in vec3 vWorldPos;
in float vAO;
flat in uint vTexIndex;

layout(binding = 0) uniform sampler2DArray uTextures;
layout(std140, binding = 0) uniform WorldUBO
{
    mat4 MVP;
    vec4 light;
    vec4 ambientData;
};

out vec4 FragColor;

#define WATER_TEXTURE_INDEX 4 // see App.cpp -> loadTextures()

void main()
{
    vec2 uv = fract(vUV);
    vec4 tex = texture(uTextures, vec3(uv, float(vTexIndex)));
    if (tex.a < 0.1) discard;

    float alpha = tex.a;
    if (vTexIndex == WATER_TEXTURE_INDEX) // Assuming WATER_TEXTURE_INDEX is defined elsewhere
    {
        alpha *= 0.5;
        if (alpha < 0.1) discard;
    }

    float dist = distance(vWorldPos, light.xyz);
    float lightFactor = clamp(1.0 - dist / light.w, 0.0, 1.0);

    float ambient = ambientData.x;
    float brightness = (ambient + lightFactor) * vAO * 1.25;

    FragColor = vec4(tex.rgb * brightness, alpha);
}
