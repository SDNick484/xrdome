#version 330 core
uniform sampler2D u_video;
uniform mat3  u_rot;        // from q_view
uniform vec2  u_tan_half;   // tan(hfov/2), tan(vfov/2)
uniform float u_u_scale;    // eye sub-image u transform: u_tex = u * scale + offset
uniform float u_u_offset;
in  vec2 v_ndc;             // -1..1 within this eye's viewport
out vec4 o_color;

void main()
{
    vec3 d = u_rot * normalize(vec3(v_ndc * u_tan_half, -1.0));
    if (d.z > 0.0) { o_color = vec4(0.0, 0.0, 0.0, 1.0); return; }
    float lon = atan(d.x, -d.z);
    float lat = asin(clamp(d.y, -1.0, 1.0));
    vec2 uv = vec2(lon / 3.14159265 + 0.5, lat / 3.14159265 + 0.5);
    // Clamp by half a texel of this eye's sub-image so GL_LINEAR never
    // reaches into the other eye's pixels.
    float m = 0.5 / (float(textureSize(u_video, 0).x) * u_u_scale);
    uv.x = clamp(uv.x, m, 1.0 - m) * u_u_scale + u_u_offset;
    o_color = texture(u_video, uv);
}
