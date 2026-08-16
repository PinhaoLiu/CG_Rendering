#version 410

uniform vec3 light_position;
uniform vec3 camera_position;

uniform vec3 ambient_colour;
uniform vec3 diffuse_colour;
uniform vec3 specular_colour;
uniform float shininess_value;

uniform sampler2D diffuse_texture;
uniform sampler2D specular_texture;
uniform sampler2D normals_texture;
uniform int has_diffuse_texture;
uniform int has_specular_texture;
uniform int has_normals_texture;
uniform int use_normal_mapping;

in VS_OUT {
	vec3 world_position;
	vec3 world_normal;
	vec3 world_tangent;
	vec3 world_binormal;
	vec2 texcoord;
} fs_in;

out vec4 frag_color;

vec3 get_world_normal()
{
	vec3 normal = normalize(fs_in.world_normal);
	if (use_normal_mapping == 0 || has_normals_texture == 0)
		return normal;

	vec3 tangent = normalize(fs_in.world_tangent
	                         - normal * dot(normal, fs_in.world_tangent));
	float handedness = dot(cross(tangent, fs_in.world_binormal), normal) < 0.0
	                 ? -1.0 : 1.0;
	vec3 binormal = normalize(cross(normal, tangent)) * handedness;
	mat3 tangent_to_world = mat3(tangent, binormal, normal);
	vec3 mapped_normal = texture(normals_texture, fs_in.texcoord).rgb * 2.0 - 1.0;
	return normalize(tangent_to_world * mapped_normal);
}

void main()
{
	vec3 diffuse_reflectance = diffuse_colour;
	if (has_diffuse_texture != 0)
		diffuse_reflectance *= texture(diffuse_texture, fs_in.texcoord).rgb;

	vec3 specular_reflectance = specular_colour;
	if (has_specular_texture != 0)
		specular_reflectance *= texture(specular_texture, fs_in.texcoord).rgb;

	vec3 normal = get_world_normal();
	vec3 light_direction = normalize(light_position - fs_in.world_position);
	vec3 view_direction = normalize(camera_position - fs_in.world_position);
	float diffuse_factor = max(dot(normal, light_direction), 0.0);

	float specular_factor = 0.0;
	if (diffuse_factor > 0.0) {
		vec3 reflected_light = reflect(-light_direction, normal);
		specular_factor = pow(max(dot(reflected_light, view_direction), 0.0),
		                      shininess_value);
	}

	vec3 colour = ambient_colour
	            + diffuse_reflectance * diffuse_factor
	            + specular_reflectance * specular_factor;
	frag_color = vec4(colour, 1.0);
}
