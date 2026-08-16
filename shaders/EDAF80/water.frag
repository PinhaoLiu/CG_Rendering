#version 410

uniform vec3 camera_position;
uniform sampler2D normal_texture;
uniform samplerCube cubemap_texture;

in VS_OUT {
	vec3 world_position;
	vec3 world_tangent;
	vec3 world_binormal;
	vec3 world_normal;
	vec2 normal_coord0;
	vec2 normal_coord1;
	vec2 normal_coord2;
} fs_in;

out vec4 frag_color;

float fresnel_term(in vec3 view_direction, in vec3 surface_normal,
	               in float incident_index, in float transmitted_index)
{
	float ratio = (incident_index - transmitted_index)
	            / (incident_index + transmitted_index);
	float r0 = ratio * ratio;
	float cosine = clamp(dot(view_direction, surface_normal), 0.0, 1.0);
	return r0 + (1.0 - r0) * pow(1.0 - cosine, 5.0);
}

void main()
{
	vec3 normal0 = texture(normal_texture, fs_in.normal_coord0).rgb * 2.0 - 1.0;
	vec3 normal1 = texture(normal_texture, fs_in.normal_coord1).rgb * 2.0 - 1.0;
	vec3 normal2 = texture(normal_texture, fs_in.normal_coord2).rgb * 2.0 - 1.0;
	vec3 bump_normal = normalize(normal0 + normal1 + normal2);

	// Keep a right-handed tangent basis on either side of the water. The
	// front face is the air side because the mesh winding points towards +y.
	float side = gl_FrontFacing ? 1.0 : -1.0;
	vec3 tangent = normalize(fs_in.world_tangent);
	vec3 binormal = normalize(fs_in.world_binormal) * side;
	vec3 geometric_normal = normalize(fs_in.world_normal) * side;
	mat3 tangent_to_world = mat3(tangent, binormal, geometric_normal);
	vec3 normal = normalize(tangent_to_world * bump_normal);

	vec3 view_direction = normalize(camera_position - fs_in.world_position);
	float facing = 1.0 - max(dot(view_direction, normal), 0.0);
	vec3 deep_colour = vec3(0.0, 0.0, 0.1);
	vec3 shallow_colour = vec3(0.0, 0.5, 0.5);
	vec3 water_colour = mix(deep_colour, shallow_colour, facing);

	const float air_index = 1.0;
	const float water_index = 1.33;
	float incident_index = gl_FrontFacing ? air_index : water_index;
	float transmitted_index = gl_FrontFacing ? water_index : air_index;
	float fresnel = fresnel_term(view_direction, normal,
	                             incident_index, transmitted_index);

	vec3 incident_direction = -view_direction;
	vec3 reflection_direction = reflect(incident_direction, normal);
	vec3 refraction_direction = refract(
		incident_direction, normal, incident_index / transmitted_index);
	vec3 reflection = texture(cubemap_texture, reflection_direction).rgb;

	vec3 refraction = vec3(0.0);
	if (dot(refraction_direction, refraction_direction) > 0.0) {
		refraction = texture(cubemap_texture, refraction_direction).rgb;
	}
	else {
		// refract() returns zero when water-to-air total internal reflection
		// occurs, in which case all of the environment contribution reflects.
		fresnel = 1.0;
	}

	vec3 colour = water_colour
	            + reflection * fresnel
	            + refraction * (1.0 - fresnel);
	frag_color = vec4(colour, 1.0);
}
