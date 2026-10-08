#version 330 core
layout(location = 0) in vec4 a_pos;
layout(location = 1) in vec2 a_uv0;
layout(location = 2) in vec4 a_shade;
layout(location = 3) in vec2 a_uv1;
out vec2 v_uv0;
out vec2 v_uv1;
out vec4 v_shade;
void main()
{
    gl_Position = a_pos;
    v_uv0 = a_uv0;
    v_uv1 = a_uv1;
    v_shade = a_shade;
}
