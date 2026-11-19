#version 330 core
// The frame sink's downscale (src/video/framesink.c): mpv's video texture into
// the 322x182 picture the depth helper reads (spec 8 of the depth 3D design).
// A box filter: each output pixel is the mean of a 4 x 4 grid of bilinear
// taps across its footprint, so a 1920 or 3840 wide film does not alias the
// way one bilinear tap per pixel would. Level 0 only (textureLod): the
// texture's chain, when there is one, is not regenerated for every frame.
uniform sampler2D u_src; // the video texture, GL-oriented (t = 1 is the top)
uniform vec2 u_out;      // the output size in pixels
out vec4 o_color;

void main()
{
    // Output row y (from the bottom of the GL target) holds picture row y from
    // the top, so glReadPixels lists the rows top first, as the ring stores them.
    vec2 cell = floor(gl_FragCoord.xy);
    vec3 sum = vec3(0.0);
    for (int j = 0; j < 4; j++) {
        for (int i = 0; i < 4; i++) {
            vec2 p = (cell + (vec2(float(i), float(j)) + 0.5) / 4.0) / u_out;
            sum += textureLod(u_src, vec2(p.x, 1.0 - p.y), 0.0).rgb;
        }
    }
    o_color = vec4(sum / 16.0, 1.0);
}
