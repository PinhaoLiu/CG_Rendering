#version 410

layout (location = 0) in vec3 vertex;
layout (location = 1) in vec3 normal;
layout (location = 2) in vec3 texcoord;
layout (location = 3) in vec3 tangent;
layout (location = 4) in vec3 binormal;

uniform mat4 vertex_model_to_world;
uniform mat4 normal_model_to_world;
uniform mat4 vertex_world_to_clip;

out VS_OUT {
	vec3 world_position;
	vec3 world_normal;
	vec3 world_tangent;
	vec3 world_binormal;
	vec2 texcoord;
} vs_out;

void main()
{
	vec4 world_position = vertex_model_to_world * vec4(vertex, 1.0);
	vs_out.world_position = world_position.xyz;
	vs_out.world_normal = mat3(normal_model_to_world) * normal;
	vs_out.world_tangent = mat3(normal_model_to_world) * tangent;
	vs_out.world_binormal = mat3(normal_model_to_world) * binormal;
	vs_out.texcoord = texcoord.xy;

	gl_Position = vertex_world_to_clip * world_position;
}
