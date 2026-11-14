#version 330 core
uniform sampler2D u_video;
uniform mat3  u_rot;        // from q_view, times this eye's convergence yaw
uniform vec2  u_tan_half;   // tan(hfov/2), tan(vfov/2)
uniform int   u_proj;       // 0: half-equirect, 1: flat screen
uniform vec2  u_inv_fov;    // half-equirect: 1/hfov, 1/vfov of the video content, in 1/radians
uniform vec2  u_reject;     // half-equirect: 1.0 on an axis whose content FOV is < 180: reject outside; 0.0: only clamp
uniform vec2  u_flat_half;  // flat: the screen's half-extents on the plane z = -1
uniform vec2  u_eye_scale;  // eye sub-image transform per axis: tex = uv * scale + offset
uniform vec2  u_eye_offset;
uniform bool  u_bicubic;    // Catmull-Rom instead of the hardware bilinear filter
uniform bool  u_dither;     // ordered dithering of the 8-bit output
uniform bool  u_trilinear;  // sample through the mipmap chain (levels 0..u_max_level)
uniform int   u_max_level;  // XRD_MIP_MAX_LEVEL
in  vec2 v_ndc;             // -1..1 within this eye's viewport
out vec4 o_color;

// This eye's sub-image in texture coordinates, set once per fragment by
// main(): tex = uv * g_sc + g_lo. It starts and ends on whole texels; on a
// split axis with an odd texel count the middle texel belongs to neither eye.
vec2 g_sc;
vec2 g_lo;

// One bilinear tap at eye-local (u, v), clamped into this eye's sub-image by
// the margin m (half a texel on a split axis), so GL_LINEAR never reaches into
// the other eye's pixels. Level 0 explicitly: the lookup runs after
// per-fragment branches, where implicit derivatives are undefined.
vec4 tap(vec2 uv, vec2 m)
{
    uv = clamp(uv, m, 1.0 - m);
    return textureLod(u_video, uv * g_sc + g_lo, 0.0);
}

// Catmull-Rom as 9 bilinear taps (3 x 3): on each axis the two middle
// weights (both >= 0) merge into one bilinear tap placed between their
// texels. Clamping every tap like tap() gives exactly clamp-to-edge
// Catmull-Rom over the eye's sub-image. size is the sub-image in texels.
vec4 catmull_rom(vec2 uv, vec2 size, vec2 m)
{
    vec2 p = uv * size;
    vec2 p1 = floor(p - 0.5) + 0.5;
    vec2 f = p - p1;
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);
    vec2 w12 = w1 + w2;
    vec2 t0 = (p1 - 1.0) / size;
    vec2 t12 = (p1 + w2 / w12) / size;
    vec2 t3 = (p1 + 2.0) / size;
    vec4 c = (tap(vec2(t0.x,  t0.y), m) * w0.x + tap(vec2(t12.x,  t0.y), m) * w12.x +
              tap(vec2(t3.x,  t0.y), m) * w3.x) * w0.y
           + (tap(vec2(t0.x, t12.y), m) * w0.x + tap(vec2(t12.x, t12.y), m) * w12.x +
              tap(vec2(t3.x, t12.y), m) * w3.x) * w12.y
           + (tap(vec2(t0.x,  t3.y), m) * w0.x + tap(vec2(t12.x,  t3.y), m) * w12.x +
              tap(vec2(t3.x,  t3.y), m) * w3.x) * w3.y;
    return clamp(c, 0.0, 1.0);
}

// Trilinear clamp margin, in level-0 texels, for a lookup whose highest
// level is k: no texel it reads holds anything from outside the eye's
// sub-image, whatever filter the driver generated the chain with, as long as
// a new texel only draws on source texels whose centres lie within its own
// footprint widened by one source texel each side (box, bilinear blit and
// 4-tap tent filters do). With t_i the size of a level-i texel in level-0
// texels, a level-k texel's content then reaches e_k from its centre, e_0 =
// 0.5 and e_k = e_(k-1) + t_k / 2 + t_(k-1); the bilinear lookup reaches one
// more t_k. Level 0 starts on the sub-image's edge, so there half a texel is
// exact.
vec2 mip_margin(int k, vec2 ts)
{
    if (k == 0)
        return vec2(0.5);
    vec2 e = vec2(0.5);
    for (int i = 1; i <= k; i++)
        e += 0.5 * ts / vec2(textureSize(u_video, i)) + ts / vec2(textureSize(u_video, i - 1));
    return e + ts / vec2(textureSize(u_video, k));
}

void main()
{
    vec3 d = u_rot * normalize(vec3(v_ndc * u_tan_half, -1.0));
    // The mapping for both projections runs without an early return, so the
    // screen-space derivatives that pick the mipmap level are taken in
    // uniform control flow; black is decided after them.
    vec2 uv;
    bool black;
    if (u_proj == 1) {
        // Flat: the screen on the plane z = -1, centred straight ahead. Behind
        // or beside (d.z >= 0), or no screen at all: black. Mirrors
        // xrd_flat_dir_to_uv. The guarded divisions keep uv finite for the
        // derivatives where the result is black anyway.
        uv = d.xy / max(-d.z, 1e-4) / (2.0 * max(u_flat_half, vec2(1e-6))) + 0.5;
        black = !(d.z < 0.0) || !(u_flat_half.x > 0.0) || !(u_flat_half.y > 0.0) ||
                any(lessThan(uv, vec2(-1e-6))) || any(greaterThan(uv, vec2(1.0 + 1e-6)));
    } else {
        // atan(0,0) is undefined in GLSL; mirror xrd_dir_to_uv at the exact pole.
        float lon = (d.x == 0.0 && d.z == 0.0) ? 0.0 : atan(d.x, -d.z);
        float lat = asin(clamp(d.y, -1.0, 1.0));
        uv = vec2(lon, lat) * u_inv_fov + 0.5;
        // Behind: black. Outside the content: black, on axes that cover less
        // than 180 degrees. The tolerance keeps the exact edge inside; a 180
        // axis only clamps, so driver atan/asin error cannot blacken pixels.
        // Mirrors xrd_dir_to_uv_fov.
        black = d.z > 0.0 ||
                any(bvec2(u_reject.x > 0.5 && (uv.x < -1e-6 || uv.x > 1.0 + 1e-6),
                          u_reject.y > 0.5 && (uv.y < -1e-6 || uv.y > 1.0 + 1e-6)));
    }
    // The eye's sub-image in whole texels. scale * ts is a whole or half
    // number: when it is half (an odd count on a split axis) the sub-image
    // shrinks to the whole texels below it and starts on the next whole
    // texel, which drops the shared middle texel. Even counts and unsplit
    // axes give exactly scale and offset.
    vec2 ts = vec2(textureSize(u_video, 0));
    vec2 size = floor(ts * u_eye_scale);
    g_sc = u_eye_scale - fract(ts * u_eye_scale) / ts;
    g_lo = u_eye_offset + (ceil(u_eye_offset * ts) - u_eye_offset * ts) / ts;
    // Level-0 texels per display pixel (uv * size is in level-0 texels, since
    // g_sc * ts == size), as GL computes it; only used with u_trilinear.
    vec2 tx = uv * size;
    float rho = max(length(dFdx(tx)), length(dFdy(tx)));
    if (black) {
        o_color = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    uv = clamp(uv, 0.0, 1.0);
    vec3 c;
    if (u_trilinear) {
        float lod = clamp(log2(clamp(rho, 1e-6, 1e6)), 0.0, float(u_max_level));
        // The highest level read: none above 0 when lod is 0 (GL magnifies
        // from level 0), else the next one up, which the blend may touch.
        int k = lod > 0.0 ? min(int(floor(lod)) + 1, u_max_level) : 0;
        // Only split axes are clamped; an unsplit one ends at the texture's
        // edge, where GL_CLAMP_TO_EDGE holds every level.
        vec2 split = vec2(u_eye_scale.x < 1.0 ? 1.0 : 0.0, u_eye_scale.y < 1.0 ? 1.0 : 0.0);
        vec2 mt = min(mip_margin(k, ts) / size, vec2(0.5)) * split;
        c = textureLod(u_video, clamp(uv, mt, 1.0 - mt) * g_sc + g_lo, lod).rgb;
    } else {
        // Half a texel of the sub-image as the clamp margin: Catmull-Rom
        // clamps both axes; bilinear clamps u, and v only when v is split (an
        // unsplit v is held at the texture edge by GL_CLAMP_TO_EDGE).
        vec2 m = 0.5 / size;
        c = u_bicubic ? catmull_rom(clamp(uv, m, 1.0 - m), size, m).rgb
                      : tap(uv, vec2(m.x, u_eye_scale.y < 1.0 ? m.y : 0.0)).rgb;
    }
    // The window is 8-bit: without dithering a 10-bit texture's extra
    // precision is rounded away here. Interleaved gradient noise gives each
    // pixel an offset in [-0.5, 0.5) of one 8-bit level.
    if (u_dither) {
        float n = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
        c += (n - 0.5) / 255.0;
    }
    // Alpha is ours, not the video texture's (RGB10_A2 has 2 alpha bits).
    o_color = vec4(c, 1.0);
}
