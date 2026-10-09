// Shared declarations for the ray traced world passes.

struct RtVertex {
    float x, y, z;
    float u, v;
    uint color;     // RGBA8, R in the high byte; A = fog factor
    uint mat;
    float pad;
};

struct Material {
    uvec4 cc;
    uvec4 colors;   // prim, env, fog, blend
    uint flags;
    float plod;
    uint tex;
    uint smp;
    uvec4 maps;     // RT material maps: orm, normal, emissive (0xffffffff = none)
    vec4 mparams;   // roughness, metallic (< 0 = from the ORM map), emissive mult, unused
};

struct Light {
    vec4 pos_radius;
    vec4 color_intensity;
};

layout(std140, set = 0, binding = 4) uniform RtUniforms {
    mat4 view;
    mat4 proj;
    mat4 inv_view;
    mat4 inv_proj;
    mat4 prev_viewproj;
    vec4 cam_pos;
    vec4 fog_color;
    uvec4 dims;      // width, height, frame index, light count
    uvec4 counts;    // opaque tris, alpha tris, history valid, spp
    vec4 params;     // ao strength, gi strength, light scale, ao distance
    vec4 params2;    // temporal alpha, denoise on, sun on, bounces
    vec4 prev_cam;   // previous frame camera position
} u;

uint pcg(inout uint state)
{
    state = state * 747796405u + 2891336453u;
    uint w = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (w >> 22u) ^ w;
}

float rand01(inout uint state)
{
    return float(pcg(state)) / 4294967296.0;
}
