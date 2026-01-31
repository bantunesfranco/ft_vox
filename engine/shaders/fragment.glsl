#version 450 core

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
    vec4 cameraPos;
};

out vec4 FragColor;

#define WATER_TEXTURE_INDEX 4 // see App.cpp -> loadTextures()
#define FOG_START 175.0
#define FOG_END 512.0
#define FOG_COLOR vec3(0.7, 0.7, 0.7)  // Light grey

float calculateFogFactor(vec3 uWorldPos, vec4 cameraPos)
{
    float cameraDistXZ = length(uWorldPos.xz - cameraPos.xz);

    // Exponential fog: gets denser faster the farther out you go
    float fogDensity = 0.015;  // Adjust this to control how fast fog builds up
    float fogFactor = 1.0 - exp(-fogDensity * (cameraDistXZ - FOG_START));

    return clamp(fogFactor, 0.0, 1.0);
}

void main()
{
    vec3 uWorldPos = vWorldPos;
    vec2 uv = vUV;//fract(vUV);
    vec4 tex = texture(uTextures, vec3(uv, float(vTexIndex)));
    if (tex.a < 0.1) discard;

    float alpha = tex.a;
    if (vTexIndex == WATER_TEXTURE_INDEX) // Assuming WATER_TEXTURE_INDEX is defined elsewhere
    {
        uWorldPos += vNormal * 0.01;
        alpha *= 0.5;
        if (alpha < 0.1) discard;
    }

    float dist = distance(uWorldPos, light.xyz);
    float lightFactor = clamp(1.0 - dist / light.w, 0.0, 1.0);

    float ambient = cameraPos.w;
    float brightness = (ambient + lightFactor) * vAO;

    vec3 color = tex.rgb * brightness;
    float fogFactor = calculateFogFactor(uWorldPos, cameraPos);

    color = mix(color, FOG_COLOR, fogFactor);

    FragColor = vec4(color, alpha);
}
