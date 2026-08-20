#version 410

layout (location = 0) in vec3 vertex_position;
layout (location = 1) in vec3 vertex_colour;

uniform mat4 uWorldToClip;

out vec3 colour;

void main()
{
	gl_Position = uWorldToClip * vec4(vertex_position, 1.0);
	colour = vertex_colour;
}
