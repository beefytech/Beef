uniform sampler2D tex;
uniform sampler2D tex2;
varying vec4 varying_color;
varying vec2 varying_texCoord0;

void main()
{
	vec4 texColor = texture2D(tex, varying_texCoord0);
	gl_FragColor = texColor * varying_color;
}