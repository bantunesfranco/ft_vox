#version 450 core

// Sky gradient: pale at the horizon (the same colour as the distance haze,
// so far terrain melts into it), the sky colour overhead.
in vec2 vNdc;

uniform mat4 uInverseViewProj;   // inverse of projection * camera rotation (no translation)
uniform vec3 uZenith;
uniform vec3 uHorizon;
uniform vec3 uSunDir;            // unit, towards the sun; the moon is opposite

out vec4 FragColor;

void main()
{
    vec4 p = uInverseViewProj * vec4(vNdc, 1.0, 1.0);
    vec3 dir = normalize(p.xyz / p.w);
    float t = smoothstep(0.0, 0.45, dir.y);
    vec3 colour = mix(uHorizon, uZenith, t);

    // Square sun and moon (Minecraft style), above the horizon only.
    vec3 side = normalize(cross(uSunDir, vec3(0.0, 0.0, 1.0)));
    vec3 up = cross(side, uSunDir);
    if (dir.y > 0.0) {
        float sunAlong = dot(dir, uSunDir);
        if (sunAlong > 0.0 && max(abs(dot(dir, side)), abs(dot(dir, up))) < 0.06 * sunAlong)
            colour = vec3(1.0, 0.95, 0.7);
        else if (sunAlong < 0.0 && max(abs(dot(dir, side)), abs(dot(dir, up))) < 0.045 * -sunAlong)
            colour = vec3(0.85, 0.87, 0.92);
    }
    FragColor = vec4(colour, 1.0);
}
