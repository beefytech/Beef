#version 100

precision lowp float;

uniform lowp sampler2D tex;
uniform lowp sampler2D tex2;
varying lowp vec4 varying_color;
varying mediump vec2 varying_texCoord0;

void main()
{
	lowp vec4 color = texture2D(tex, varying_texCoord0);
	gl_FragColor = color * varying_color;
}