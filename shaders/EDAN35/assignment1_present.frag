#version 460

layout(location = 0) in vec2 vertex_uv;
layout(location = 0) out vec4 fragment_colour;

uniform sampler2D uImage;

void main()
{
	// The ray tracer uses image row zero as the top row; OpenGL textures use it as the bottom row.
	vec3 linear_colour = texture(uImage, vec2(vertex_uv.x, 1.0 - vertex_uv.y)).rgb;
	vec3 gamma_corrected = pow(clamp(linear_colour, 0.0, 1.0), vec3(1.0 / 2.2));
	fragment_colour = vec4(gamma_corrected, 1.0);
}
