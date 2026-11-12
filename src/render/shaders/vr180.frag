#version 330 core
uniform sampler2D u_video;
uniform mat3  u_rot;        // from q_view
uniform vec2  u_tan_half;   // tan(hfov/2), tan(vfov/2)
uniform vec2  u_inv_fov;    // 1/hfov, 1/vfov of the video content, in 1/radians
uniform vec2  u_reject;     // 1.0 on an axis whose content FOV is < 180: reject outside; 0.0: only clamp
uniform float u_u_scale;    // eye sub-image u transform: u_tex = u * scale + offset
uniform float u_u_offset;
in  vec2 v_ndc;             // -1..1 within this eye's viewport
out vec4 o_color;

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
    // Clamp by half a texel of this eye's sub-image so GL_LINEAR never
    // reaches into the other eye's pixels.
    float m = 0.5 / (float(textureSize(u_video, 0).x) * u_u_scale);
    uv.x = clamp(uv.x, m, 1.0 - m) * u_u_scale + u_u_offset;
    o_color = texture(u_video, uv);
}
