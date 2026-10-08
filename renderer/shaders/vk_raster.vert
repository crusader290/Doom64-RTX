#version 450
layout(location = 0) in vec4 a_pos;
layout(location = 1) in vec2 a_uv0;
layout(location = 2) in vec4 a_shade;
layout(location = 3) in vec2 a_uv1;
layout(location = 0) out vec2 v_uv0;
layout(location = 1) out vec2 v_uv1;
layout(location = 2) out vec4 v_shade;
void main()
{
    // Interpreter output uses OpenGL clip conventions; Vulkan wants z in [0, w].
    // Y is flipped with a negative viewport height.
    gl_Position = vec4(a_pos.xy, (a_pos.z + a_pos.w) * 0.5, a_pos.w);
    v_uv0 = a_uv0;
    v_uv1 = a_uv1;
    v_shade = a_shade;
}
