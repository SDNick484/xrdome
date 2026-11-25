#version 300 es
#extension GL_OES_EGL_image_external_essl3 : require
precision highp float;
precision highp int;

// The copy pass: one new video frame (the decoder's SurfaceTexture, an OES texture) into the
// RGB10_A2 texture the shared eye shader samples. Drawn with vr180.vert's fullscreen
// triangle, so v_ndc is -1..1 over the target.
uniform samplerExternalOES u_src;
uniform mat4 u_st;      // SurfaceTexture.getTransformMatrix()
in  vec2 v_ndc;
out vec4 o_color;

void main()
{
    vec2 uv = v_ndc * 0.5 + 0.5;
    o_color = texture(u_src, (u_st * vec4(uv, 0.0, 1.0)).xy);
}
