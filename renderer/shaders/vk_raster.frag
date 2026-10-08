#version 450
#extension GL_GOOGLE_include_directive : require
layout(location = 0) in vec2 v_uv0;
layout(location = 1) in vec2 v_uv1;
layout(location = 2) in vec4 v_shade;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = 0) uniform sampler2D u_tex0;
layout(set = 1, binding = 0) uniform sampler2D u_tex1;
layout(push_constant) uniform PC {
    uvec4 cc;
    uvec4 colors;   // prim, env, fog, blend
    uint flags;
    float plod;
} pc;
#include "combiner.glsl"
vec4 sample_n64(sampler2D t, vec2 uv)
{
    if ((pc.flags & F_FILTER) != 0u)
        uv += 0.5 / vec2(textureSize(t, 0));
    return texture(t, uv);
}
void main()
{
    vec4 t0 = sample_n64(u_tex0, v_uv0);
    vec4 t1 = sample_n64(u_tex1, v_uv1);
    vec4 c;
    if (!n64_pixel(pc.cc, pc.flags, pc.colors.x, pc.colors.y, pc.colors.z, pc.colors.w, pc.plod, t0, t1, v_shade, c))
        discard;
    o_color = c;
}
