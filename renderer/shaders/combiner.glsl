// N64 colour combiner + blender emulation shared by the OpenGL and Vulkan
// raster shaders. Selector values match D64GFX_CC_* in d64gfx.h.
//   cycle: rgb = (a - b) * c + d,  alpha = (a - b) * c + d

vec3 cc_rgb(int s, vec4 comb, vec4 t0, vec4 t1, vec4 prim, vec4 shade, vec4 env, float plod)
{
    if (s == 0) return comb.rgb;
    if (s == 1) return t0.rgb;
    if (s == 2) return t1.rgb;
    if (s == 3) return prim.rgb;
    if (s == 4) return shade.rgb;
    if (s == 5) return env.rgb;
    if (s == 6) return vec3(1.0);
    if (s == 7) return vec3(0.0);
    if (s == 8) return vec3(comb.a);
    if (s == 9) return vec3(t0.a);
    if (s == 10) return vec3(t1.a);
    if (s == 11) return vec3(prim.a);
    if (s == 12) return vec3(shade.a);
    if (s == 13) return vec3(env.a);
    if (s == 14) return vec3(0.0);
    if (s == 15) return vec3(plod);
    return vec3(0.5);
}

float cc_a(int s, vec4 comb, vec4 t0, vec4 t1, vec4 prim, vec4 shade, vec4 env, float plod)
{
    if (s == 0 || s == 8) return comb.a;
    if (s == 1 || s == 9) return t0.a;
    if (s == 2 || s == 10) return t1.a;
    if (s == 3 || s == 11) return prim.a;
    if (s == 4 || s == 12) return shade.a;
    if (s == 5 || s == 13) return env.a;
    if (s == 6) return 1.0;
    if (s == 7) return 0.0;
    if (s == 15) return plod;
    return 0.0;
}

vec4 cc_cycle(ivec4 crgb, ivec4 calpha, vec4 comb, vec4 t0, vec4 t1, vec4 prim, vec4 shade, vec4 env, float plod)
{
    vec3 a = cc_rgb(crgb.x, comb, t0, t1, prim, shade, env, plod);
    vec3 b = cc_rgb(crgb.y, comb, t0, t1, prim, shade, env, plod);
    vec3 c = cc_rgb(crgb.z, comb, t0, t1, prim, shade, env, plod);
    vec3 d = cc_rgb(crgb.w, comb, t0, t1, prim, shade, env, plod);
    float aa = cc_a(calpha.x, comb, t0, t1, prim, shade, env, plod);
    float ab = cc_a(calpha.y, comb, t0, t1, prim, shade, env, plod);
    float ac = cc_a(calpha.z, comb, t0, t1, prim, shade, env, plod);
    float ad = cc_a(calpha.w, comb, t0, t1, prim, shade, env, plod);
    return clamp(vec4((a - b) * c + d, (aa - ab) * ac + ad), 0.0, 1.0);
}

// flags (D64GFX_CMD_*)
const uint F_BLEND = 1u;
const uint F_FOG = 2u;
const uint F_ALPHA_CVG = 4u;
const uint F_ALPHA_THRESH = 8u;
const uint F_TWO_CYCLE = 16u;
const uint F_FILTER = 32u;

vec4 unpack_rgba(uint c)
{
    return vec4(float((c >> 24) & 255u), float((c >> 16) & 255u), float((c >> 8) & 255u), float(c & 255u)) / 255.0;
}

ivec4 unpack_sel(uint w)
{
    return ivec4(int(w & 255u), int((w >> 8) & 255u), int((w >> 16) & 255u), int((w >> 24) & 255u));
}

// Full N64 pixel pipeline for one fragment. Returns false to discard.
bool n64_pixel(uvec4 cc, uint flags, uint prim_u, uint env_u, uint fog_u, uint blend_u, float plod,
               vec4 t0, vec4 t1, vec4 shade, out vec4 result)
{
    vec4 prim = unpack_rgba(prim_u);
    vec4 env = unpack_rgba(env_u);
    vec4 c0 = cc_cycle(unpack_sel(cc.x), unpack_sel(cc.y), vec4(0.0), t0, t1, prim, shade, env, plod);
    vec4 c = c0;
    if ((flags & F_TWO_CYCLE) != 0u)
        c = cc_cycle(unpack_sel(cc.z), unpack_sel(cc.w), c0, t0, t1, prim, shade, env, plod);
    if ((flags & F_ALPHA_CVG) != 0u && c.a < 0.5)
        return false;
    if ((flags & F_ALPHA_THRESH) != 0u && c.a < max(unpack_rgba(blend_u).a, 1.0 / 255.0))
        return false;
    if ((flags & F_FOG) != 0u)
        c.rgb = mix(c.rgb, unpack_rgba(fog_u).rgb, shade.a);
    result = c;
    return true;
}
