#version 330 core
uniform sampler2D u_video;
uniform mat3  u_rot;        // from q_view, times this eye's convergence yaw
uniform vec2  u_tan_half;   // tan(hfov/2), tan(vfov/2)
uniform vec2  u_inv_fov;    // 1/hfov, 1/vfov of the video content, in 1/radians
uniform vec2  u_reject;     // 1.0 on an axis whose content FOV is < 180: reject outside; 0.0: only clamp
uniform float u_u_scale;    // eye sub-image u transform: u_tex = u * scale + offset
uniform float u_u_offset;
uniform bool  u_bicubic;    // Catmull-Rom instead of the hardware bilinear filter
uniform bool  u_dither;     // ordered dithering of the 8-bit output
in  vec2 v_ndc;             // -1..1 within this eye's viewport
out vec4 o_color;

// One bilinear tap at eye-local (u, v): u is clamped into this eye's
// sub-image by the half-texel margin m.x, so GL_LINEAR never reaches into
// the other eye's pixels; v is clamped to the texture the same way.
vec4 tap(vec2 uv, vec2 m)
{
    uv = clamp(uv, m, 1.0 - m);
    return texture(u_video, vec2(uv.x * u_u_scale + u_u_offset, uv.y));
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
    if (d.z > 0.0) { o_color = vec4(0.0, 0.0, 0.0, 1.0); return; }
    // atan(0,0) is undefined in GLSL; mirror xrd_dir_to_uv at the exact pole.
    float lon = (d.x == 0.0 && d.z == 0.0) ? 0.0 : atan(d.x, -d.z);
    float lat = asin(clamp(d.y, -1.0, 1.0));
    vec2 uv = vec2(lon, lat) * u_inv_fov + 0.5;
    // Outside the content: black, on axes that cover less than 180 degrees. The
    // tolerance keeps the exact edge inside; a 180 axis only clamps, so driver
    // atan/asin error cannot blacken pixels. Mirrors xrd_dir_to_uv_fov.
    if (any(bvec2(u_reject.x > 0.5 && (uv.x < -1e-6 || uv.x > 1.0 + 1e-6),
                  u_reject.y > 0.5 && (uv.y < -1e-6 || uv.y > 1.0 + 1e-6)))) {
        o_color = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    uv = clamp(uv, 0.0, 1.0);
    // The eye's sub-image in texels, and half a texel of it as the clamp
    // margin: the seam clamp on u, the texture edge on v.
    vec2 size = vec2(textureSize(u_video, 0)) * vec2(u_u_scale, 1.0);
    vec2 m = 0.5 / size;
    vec3 c = u_bicubic ? catmull_rom(clamp(uv, m, 1.0 - m), size, m).rgb
                       : tap(uv, vec2(m.x, 0.0)).rgb;
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
