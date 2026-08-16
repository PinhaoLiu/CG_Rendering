#version 410

layout (location = 0) in vec3 vertex;
layout (location = 2) in vec3 texcoord;

uniform mat4 vertex_model_to_world;
uniform mat4 vertex_world_to_clip;

out VS_OUT {
	vec3 world_position;
	vec2 texcoord;
} vs_out;

void main()
{
	vec4 world_position = vertex_model_to_world * vec4(vertex, 1.0);
	vs_out.world_position = world_position.xyz;
	vs_out.texcoord = texcoord.xy;
	gl_Position = vertex_world_to_clip * world_position;
}
