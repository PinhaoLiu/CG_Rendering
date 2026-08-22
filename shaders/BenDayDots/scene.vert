#version 410

uniform mat4 vertex_model_to_world;
uniform mat4 vertex_world_to_clip;
uniform mat3 normal_model_to_world;

layout (location = 0) in vec3 vertex;
layout (location = 1) in vec3 normal;
layout (location = 2) in vec3 texcoord;

out VS_OUT
{
	vec3 world_normal;
	vec2 texcoord;
} vs_out;

void main()
{
	vs_out.world_normal = normalize(normal_model_to_world * normal);
	vs_out.texcoord = texcoord.xy;
	gl_Position = vertex_world_to_clip * vertex_model_to_world * vec4(vertex, 1.0);
}
