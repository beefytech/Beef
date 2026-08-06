#version 150 core

uniform sampler2D tex;
uniform sampler2D tex2;
in vec4 varying_color;
in vec2 varying_texCoord0;

out vec4 fragColor;

void main()
{
	vec4 texColor = texture(tex, varying_texCoord0);
	float gray = varying_color.r * 0.299 + varying_color.g * 0.587 + varying_color.b * 0.114;
	float a = mix(texColor.a, texColor.r, gray);
    fragColor = vec4(a, a, a, a) * varying_color;
}