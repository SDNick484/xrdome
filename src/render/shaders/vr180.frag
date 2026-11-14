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

void main()
{
    vec3 d = u_rot * normalize(vec3(v_ndc * u_tan_half, -1.0));
    vec2 uv;
    if (u_proj == 1) {
        // Flat: the screen on the plane z = -1, centred straight ahead. Behind
        // or beside (d.z >= 0), or no screen at all: black. Mirrors
        // xrd_flat_dir_to_uv.
        if (!(d.z < 0.0) || !(u_flat_half.x > 0.0) || !(u_flat_half.y > 0.0)) {
            o_color = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }
        uv = d.xy / -d.z / (2.0 * u_flat_half) + 0.5;
        if (any(lessThan(uv, vec2(-1e-6))) || any(greaterThan(uv, vec2(1.0 + 1e-6)))) {
            o_color = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }
    } else {
        if (d.z > 0.0) { o_color = vec4(0.0, 0.0, 0.0, 1.0); return; }
        // atan(0,0) is undefined in GLSL; mirror xrd_dir_to_uv at the exact pole.
        float lon = (d.x == 0.0 && d.z == 0.0) ? 0.0 : atan(d.x, -d.z);
        float lat = asin(clamp(d.y, -1.0, 1.0));
        uv = vec2(lon, lat) * u_inv_fov + 0.5;
        // Outside the content: black, on axes that cover less than 180 degrees. The
        // tolerance keeps the exact edge inside; a 180 axis only clamps, so driver
        // atan/asin error cannot blacken pixels. Mirrors xrd_dir_to_uv_fov.
        if (any(bvec2(u_reject.x > 0.5 && (uv.x < -1e-6 || uv.x > 1.0 + 1e-6),
                      u_reject.y > 0.5 && (uv.y < -1e-6 || uv.y > 1.0 + 1e-6)))) {
            o_color = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }
    }
    uv = clamp(uv, 0.0, 1.0);
    // The eye's sub-image in whole texels. scale * ts is a whole or half
    // number: when it is half (an odd count on a split axis) the sub-image
    // shrinks to the whole texels below it and starts on the next whole
    // texel, which drops the shared middle texel. Even counts and unsplit
    // axes give exactly scale and offset.
    vec2 ts = vec2(textureSize(u_video, 0));
    vec2 size = floor(ts * u_eye_scale);
    g_sc = u_eye_scale - fract(ts * u_eye_scale) / ts;
    g_lo = u_eye_offset + (ceil(u_eye_offset * ts) - u_eye_offset * ts) / ts;
    // Half a texel of the sub-image as the clamp margin: Catmull-Rom clamps
    // both axes; bilinear clamps u, and v only when v is split (an unsplit v
    // is held at the texture edge by GL_CLAMP_TO_EDGE).
    vec2 m = 0.5 / size;
    vec3 c = u_bicubic ? catmull_rom(clamp(uv, m, 1.0 - m), size, m).rgb
                       : tap(uv, vec2(m.x, u_eye_scale.y < 1.0 ? m.y : 0.0)).rgb;
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
