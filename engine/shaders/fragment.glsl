#version 450 core
layout(early_fragment_tests) in;

in vec2 vUV;
in vec3 vNormal;
in vec3 vWorldPos;
flat in uint vTexIndex;

layout(binding = 0) uniform sampler2DArray uTextures;

layout(std140, binding = 0) uniform WorldUBO
{
    mat4 MVP;
    vec4 light;       // xyz = position, w = radius
    vec4 ambientData; // x = ambient, yzw = padding
};

out vec4 FragColor;

void main()
{
    vec2 uv = clamp(vUV, 0.001, 0.999);
    vec4 tex = texture(uTextures, vec3(uv, vTexIndex));

    vec3 lightDir = light.xyz - vWorldPos;
    float distanceToLight = length(lightDir);
    lightDir = normalize(lightDir);

    // Lambertian diffuse term
    float diffuse = max(dot(normalize(vNormal), lightDir), 0.0);

    float attenuation = clamp(1.0 - distanceToLight / light.w, 0.0, 1.0);

    float ambient = ambientData.x;
    float lighting = max(ambient, diffuse * attenuation);

    FragColor = vec4(tex.rgb * lighting, tex.a);
}
