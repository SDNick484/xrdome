#version 330 core
// Contrast-adaptive sharpening of the eye pass's picture (--sharpen), the
// second pass of xrd_renderer_draw. The filter in main() is a GLSL port of
// the sharpen-only path of AMD FidelityFX CAS (CasFilter with noScaling,
// CAS_BETTER_DIAGONALS, CAS_SLOW and CAS_GO_SLOWER: per-channel weights,
// exact reciprocal and square root), from ffx_cas.h 1.20190610, under the
// licence below. The eye clamp, the picture mask, the strength mapping and
// the dithering are xrdome's.
//
// Copyright (c) 2017-2019 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software
// and associated documentation files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
// BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
uniform sampler2D u_src;  // the eye pass's output, the target's size; alpha 0 where it drew black
uniform ivec2 u_lo;       // this eye's viewport: its first pixel
uniform ivec2 u_hi;       // and its last
uniform float u_peak;     // CAS's negative lobe at full amount: -strength / 5
uniform bool  u_dither;   // ordered dithering of the 8-bit output, as in vr180.frag
out vec4 o_color;

vec3 g_e; // the centre pixel

// The neighbour at p. Reads stay inside this eye's viewport (clamp to its
// edge, so nothing crosses between the eyes), and a neighbour the eye pass
// drew black (outside the picture or behind) counts as the centre, so the
// picture's own edge is sharpened as if it continued, and black never is.
vec3 nb(ivec2 p)
{
    vec4 t = texelFetch(u_src, clamp(p, u_lo, u_hi), 0);
    return t.a > 0.5 ? t.rgb : g_e;
}

void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec4 c = texelFetch(u_src, p, 0);
    if (c.a < 0.5) {
        // Black drawn by the eye pass stays black and undithered.
        o_color = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    g_e = c.rgb;
    // a b c
    // d e f
    // g h i
    vec3 a = nb(p + ivec2(-1, -1)), b = nb(p + ivec2(0, -1)), cc = nb(p + ivec2(1, -1));
    vec3 d = nb(p + ivec2(-1, 0)), e = g_e, f = nb(p + ivec2(1, 0));
    vec3 g = nb(p + ivec2(-1, 1)), h = nb(p + ivec2(0, 1)), i = nb(p + ivec2(1, 1));
    // Soft min and max: the cross's plus the 3 x 3's (twice the average of the two).
    vec3 mn = min(min(min(d, e), min(f, b)), h);
    vec3 mn2 = min(min(min(mn, a), min(cc, g)), i);
    mn += mn2;
    vec3 mx = max(max(max(d, e), max(f, b)), h);
    vec3 mx2 = max(max(max(mx, a), max(cc, g)), i);
    mx += mx2;
    // Smooth minimum distance to the signal limit divided by the smooth
    // maximum, shaped by a square root. The max() only keeps a black centre
    // with black neighbours finite (its amount is 0 either way).
    vec3 amp = sqrt(clamp(min(mn, 2.0 - mx) / max(mx, vec3(1e-5)), 0.0, 1.0));
    // Filter shape:
    //  0 w 0
    //  w 1 w
    //  0 w 0
    vec3 w = amp * u_peak;
    vec3 o = clamp((b * w + d * w + f * w + h * w + e) / (1.0 + 4.0 * w), 0.0, 1.0);
    if (u_dither) {
        float n = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
        o += (n - 0.5) / 255.0;
    }
    o_color = vec4(o, 1.0);
}
