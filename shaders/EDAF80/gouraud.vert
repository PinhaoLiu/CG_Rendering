#version 410

layout (location = 0) in vec3 vertex;
layout (location = 1) in vec3 normal;
layout (location = 2) in vec3 texcoord;

uniform mat4 vertex_model_to_world;
uniform mat4 normal_model_to_world;
uniform mat4 vertex_world_to_clip;

uniform vec3 light_position;
uniform vec3 camera_position;
uniform float shininess_value;

out VS_OUT {
	float diffuse_factor;
	float specular_factor;
	vec2 texcoord;
} vs_out;

void main()
{
	vec4 world_position = vertex_model_to_world * vec4(vertex, 1.0);
	vec3 world_normal = normalize(mat3(normal_model_to_world) * normal);
	vec3 light_direction = normalize(light_position - world_position.xyz);
	vec3 view_direction = normalize(camera_position - world_position.xyz);

	vs_out.diffuse_factor = max(dot(world_normal, light_direction), 0.0);
	vs_out.specular_factor = 0.0;
	if (vs_out.diffuse_factor > 0.0) {
		vec3 reflected_light = reflect(-light_direction, world_normal);
		vs_out.specular_factor = pow(
			max(dot(reflected_light, view_direction), 0.0), shininess_value);
	}
	vs_out.texcoord = texcoord.xy;

	gl_Position = vertex_world_to_clip * world_position;
}
