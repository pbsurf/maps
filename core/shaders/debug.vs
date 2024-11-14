#ifdef GL_ES
precision highp float;
#endif

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_proj;

attribute vec4 a_position;
attribute vec4 a_color;

varying vec4 v_color;

void main() {

    v_color = a_color;

    gl_Position = u_proj * u_view * u_model * a_position;
    // adjust depth so that modes w/ depth enabled work (will have no effect for overlay mode of course)
    gl_Position.z += -1010.0 * (TANGRAM_DEPTH_DELTA * gl_Position.w - 0.02*u_proj[2][3]);
}
