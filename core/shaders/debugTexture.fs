#ifdef GL_ES
precision mediump float;
#endif

uniform float u_scale;
uniform sampler2D u_tex;
varying vec2 uv;

void main() {
    gl_FragColor = u_scale*texture2D(u_tex, uv);
}

