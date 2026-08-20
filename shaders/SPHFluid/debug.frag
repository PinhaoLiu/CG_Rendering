#version 410

in vec3 colour;

layout (location = 0) out vec4 frag_colour;

void main()
{
	frag_colour = vec4(colour, 1.0);
}
