#version 450 core

// One triangle covering the whole screen, on the far plane.
out vec2 vNdc;

void main()
{
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;
    vNdc = p;
    gl_Position = vec4(p, 1.0, 1.0);
}
