#version 100

precision lowp float;

uniform lowp sampler2D tex;
uniform lowp sampler2D tex2;
varying lowp vec4 varying_color;
varying mediump vec2 varying_texCoord0;

void main()
{
	lowp vec4 texColor = texture2D(tex, varying_texCoord0);
	float gray = varying_color.r * 0.299 + varying_color.g * 0.587 + varying_color.b * 0.114;
	float a = mix(texColor.a, texColor.r, gray);
    gl_FragColor = vec4(a, a, a, a) * varying_color;
}