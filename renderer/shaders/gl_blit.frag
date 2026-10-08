#version 330 core
in vec2 v_uv;
out vec4 o_color;
uniform sampler2D u_src;
void main() { o_color = vec4(texture(u_src, v_uv).rgb, 1.0); }
