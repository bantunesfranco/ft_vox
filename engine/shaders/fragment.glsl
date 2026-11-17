#version 450 core

// No forced early_fragment_tests: it writes depth before `discard` runs, so the
// see-through pixels of leaves would hide everything behind them.

in vec2 vUV;
in vec3 vNormal;
in vec3 vWorldPos;
in float vAO;
in float vSky;
in float vBlock;
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
    vec4 time;        // x = daylight: how much of the sky light is there now
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

    // Light level 0..15: sky light scaled by the time of day, or block light.
    // Minecraft's curve (v / (4 - 3v)) makes dim levels fall off quickly;
    // level 0 keeps 5 %. Full light is 0.625, the brightness before light levels.
    float level = max(vSky * time.x, vBlock) / 15.0;
    float curve = level / (4.0 - 3.0 * level);
    float brightness = 0.625 * (0.05 + 0.95 * curve) * vAO;

    vec3 color = tex.rgb * brightness;
    if (fog.w <= 0.0 && skyFog.w > 0.0) {
        // Horizontal distance: columns load in a square around the player,
        // the last 25 % of the view distance fades into the haze.
        float d = distance(vWorldPos.xz, eye.xz);
        color = mix(color, skyFog.rgb, smoothstep(skyFog.w * 0.75, skyFog.w, d));
    }
    if (fog.w > 0.0) {
        // Clear close by, easing into the fog colour by fog.w.
        float f = smoothstep(fog.w * 0.1, fog.w, distance(vWorldPos, eye.xyz));
        color = mix(color, fog.rgb, f);
    }
    FragColor = vec4(color, tex.a);
}
