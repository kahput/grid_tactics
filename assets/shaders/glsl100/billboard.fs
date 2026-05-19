#version 100

precision mediump float;

varying vec2 fragTexCoord;
varying vec4 fragColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

void main() {
    vec4 texelColor = texture2D(texture0, fragTexCoord);
    if (texelColor.a < 0.1) discard;  
    gl_FragColor = texelColor*colDiffuse;
}
