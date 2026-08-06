#version 150 core

uniform mat4 screenMatrix;

in vec4 position;
in vec2 texCoord0;
in vec4 color;

out vec4 varying_color;
out vec2 varying_texCoord0;

void main()
{        
	gl_Position = screenMatrix * position;    
    varying_color = vec4(color.b * color.a, color.g * color.a, color.r * color.a, color.a);    
    varying_texCoord0 = texCoord0;
} 
