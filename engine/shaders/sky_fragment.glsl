#version 450 core

// Sky gradient: pale at the horizon (the same colour as the distance haze,
// so far terrain melts into it), the sky colour overhead.
in vec2 vNdc;

uniform mat4 uInverseViewProj;   // inverse of projection * camera rotation (no translation)
uniform vec3 uZenith;
uniform vec3 uHorizon;

out vec4 FragColor;

void main()
{
    vec4 p = uInverseViewProj * vec4(vNdc, 1.0, 1.0);
    vec3 dir = normalize(p.xyz / p.w);
    float t = smoothstep(0.0, 0.45, dir.y);
    FragColor = vec4(mix(uHorizon, uZenith, t), 1.0);
}
