#version 330 core
out vec2 v_ndc;

void main()
{
    /* Fullscreen triangle from gl_VertexID: (-1,-1) (3,-1) (-1,3). */
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    v_ndc = p * 2.0 - 1.0;
    gl_Position = vec4(v_ndc, 0.0, 1.0);
}
