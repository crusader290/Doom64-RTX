#version 330 core
in vec2 v_uv0;
in vec2 v_uv1;
in vec4 v_shade;
out vec4 o_color;
uniform sampler2D u_tex0;
uniform sampler2D u_tex1;
uniform uvec4 u_cc;
uniform uvec4 u_colors; // prim, env, fog, blend
uniform uint u_flags;
uniform float u_plod;
//COMBINER//
vec4 sample_n64(sampler2D t, vec2 uv)
{
    if ((u_flags & F_FILTER) != 0u)
        uv += 0.5 / vec2(textureSize(t, 0));
    return texture(t, uv);
}
void main()
{
    vec4 t0 = sample_n64(u_tex0, v_uv0);
    vec4 t1 = sample_n64(u_tex1, v_uv1);
    vec4 c;
    if (!n64_pixel(u_cc, u_flags, u_colors.x, u_colors.y, u_colors.z, u_colors.w, u_plod, t0, t1, v_shade, c))
        discard;
    o_color = c;
}
