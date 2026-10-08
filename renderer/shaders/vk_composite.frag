#version 450
// Composites the ray traced world image over the rasterised sky and writes
// its depth so later translucent world draws are depth tested against it.
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = 0) uniform sampler2D u_rt;
layout(set = 0, binding = 1) uniform sampler2D u_depth;
void main()
{
    vec4 c = texture(u_rt, v_uv);
    if (c.a <= 0.0)
        discard;
    o_color = c;
    gl_FragDepth = texture(u_depth, v_uv).r;
}
