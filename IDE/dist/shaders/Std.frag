#version 150 core

uniform sampler2D tex;
uniform sampler2D tex2;

in vec4 varying_color;
in vec2 varying_texCoord0;

out vec4 fragColor;

void main()
{
	vec4 texColor = texture(tex, varying_texCoord0);
	fragColor = texColor * varying_color;
}