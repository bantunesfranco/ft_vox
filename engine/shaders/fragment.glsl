#version 450 core

// No forced early_fragment_tests: it writes depth before `discard` runs, so the
// see-through pixels of leaves would hide everything behind them.

in vec2 vUV;
in vec3 vNormal;
in vec3 vWorldPos;
in float vAO;
flat in uint vTexIndex;

layout(binding = 0) uniform sampler2D uAtlas;
uniform vec4 uTextureRects[64];
uniform bool uHighlightPass;

layout(std140, binding = 0) uniform WorldUBO
{
    mat4 MVP;
    vec4 light;
    vec4 ambientData;
    vec4 eye;         // xyz = camera position
    vec4 fog;         // rgb = colour, w = distance (0 = off): eye inside water / lava
    vec4 skyFog;      // rgb = colour, w = view distance (0 = off): far terrain fades out
};

out vec4 FragColor;

void main()
{
    if (uHighlightPass) {
        FragColor = vec4(1.0, 0.82, 0.12, 1.0);
        return;
    }

    vec4 rect = uTextureRects[vTexIndex];
    vec2 atlasUV = rect.xy + fract(vUV) * rect.zw;
    vec4 tex = texture(uAtlas, atlasUV);

    // The grass-top sprite is grayscale in the atlas and needs its biome tint.
    if (vTexIndex == 0u)
        tex.rgb *= vec3(0.65, 0.99, 0.45);

    if (vTexIndex == 6u) {
        tex.rgb *= vec3(0.25, 0.55, 0.9);
        tex.a *= 0.75;
    }

    if (tex.a < 0.1)
    discard;

    float dist = distance(vWorldPos, light.xyz);

    float lightFactor = clamp(1.0 - dist / light.w, 0.0, 1.0);

    float ambient = ambientData.x;

    float brightness = (ambient + lightFactor) * vAO * 1.25;

    vec3 color = tex.rgb * brightness;
    if (fog.w <= 0.0 && skyFog.w > 0.0) {
        // Horizontal distance: columns load in a square around the player,
        // the last 40 % of the view distance fades into the haze.
        float d = distance(vWorldPos.xz, eye.xz);
        color = mix(color, skyFog.rgb, smoothstep(skyFog.w * 0.6, skyFog.w, d));
    }
    if (fog.w > 0.0) {
        float f = clamp(distance(vWorldPos, eye.xyz) / fog.w, 0.0, 1.0);
        color = mix(color, fog.rgb, f);
    }
    FragColor = vec4(color, tex.a);
}
