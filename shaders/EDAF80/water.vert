#version 410

layout (location = 0) in vec3 vertex;
layout (location = 1) in vec3 normal;
layout (location = 2) in vec3 texcoord;
layout (location = 3) in vec3 tangent;
layout (location = 4) in vec3 binormal;

uniform mat4 vertex_model_to_world;
uniform mat4 normal_model_to_world;
uniform mat4 vertex_world_to_clip;
uniform float elapsed_time_s;
uniform int surface_type;

out VS_OUT {
	vec3 world_position;
	vec3 world_tangent;
	vec3 world_binormal;
	vec3 world_normal;
	vec2 normal_coord0;
	vec2 normal_coord1;
	vec2 normal_coord2;
} vs_out;

void evaluate_wave(in vec2 position, in vec2 direction,
	               in float amplitude, in float frequency,
	               in float phase, in float sharpness, in float time,
	               out float height, out vec2 derivative)
{
	float angle = dot(position, direction) * frequency + phase * time;
	float alpha = sin(angle) * 0.5 + 0.5;
	float common_derivative = 0.5 * sharpness * frequency * amplitude
	                        * pow(alpha, sharpness - 1.0) * cos(angle);

	height = amplitude * pow(alpha, sharpness);
	derivative = common_derivative * direction;
}

void main()
{
	float wave_height0;
	float wave_height1;
	vec2 wave_derivative0;
	vec2 wave_derivative1;

	evaluate_wave(vertex.xz, vec2(-1.0, 0.0),
	              1.0, 0.2, 0.5, 2.0, elapsed_time_s,
	              wave_height0, wave_derivative0);
	evaluate_wave(vertex.xz, vec2(-0.7, 0.7),
	              0.5, 0.4, 1.3, 2.0, elapsed_time_s,
	              wave_height1, wave_derivative1);

	float height = wave_height0 + wave_height1;
	vec2 derivative = wave_derivative0 + wave_derivative1;
	vec3 displaced_vertex;
	vec3 local_tangent;
	vec3 local_binormal;
	vec3 local_normal;

	if (surface_type == 1) {
		// On a sphere, move each vertex radially along its original normal.
		// The wave still varies with model-space x and z, so its gradient can
		// be projected onto the sphere's tangent directions.
		vec3 base_normal = normalize(normal);
		vec3 base_tangent = normalize(
			tangent - base_normal * dot(base_normal, tangent));
		float handedness = dot(cross(base_tangent, binormal), base_normal) < 0.0
		                 ? -1.0 : 1.0;
		vec3 base_binormal = normalize(cross(base_normal, base_tangent))
		                     * handedness;

		displaced_vertex = vertex + height * base_normal;

		vec3 wave_gradient = vec3(derivative.x, 0.0, derivative.y);
		float tangent_derivative = dot(wave_gradient, base_tangent);
		float binormal_derivative = dot(wave_gradient, base_binormal);
		float radial_scale = 1.0 + height / max(length(vertex), 0.0001);

		local_tangent = radial_scale * base_tangent
		              + tangent_derivative * base_normal;
		local_binormal = radial_scale * base_binormal
		               + binormal_derivative * base_normal;
		local_normal = cross(local_tangent, local_binormal);
	}
	else {
		displaced_vertex = vertex + vec3(0.0, height, 0.0);

		// For P(x,z,t) = (x,H(x,z,t),z), these are the analytical
		// tangent-space basis vectors from the seminar. cross(B,T) gives
		// the upward-facing normal (-dH/dx, 1, -dH/dz).
		local_tangent = vec3(1.0, derivative.x, 0.0);
		local_binormal = vec3(0.0, derivative.y, 1.0);
		local_normal = cross(local_binormal, local_tangent);
	}

	vs_out.world_tangent = normalize(mat3(vertex_model_to_world) * local_tangent);
	vs_out.world_binormal = normalize(mat3(vertex_model_to_world) * local_binormal);
	vs_out.world_normal = normalize(mat3(normal_model_to_world) * local_normal);

	vec4 world_position = vertex_model_to_world * vec4(displaced_vertex, 1.0);
	vs_out.world_position = world_position.xyz;

	vec2 texture_scale = vec2(8.0, 4.0);
	float normal_time = mod(elapsed_time_s, 100.0);
	vec2 normal_speed = vec2(-0.05, 0.0);
	vs_out.normal_coord0 = texcoord.xy * texture_scale
	                     + normal_time * normal_speed;
	vs_out.normal_coord1 = texcoord.xy * texture_scale * 2.0
	                     + normal_time * normal_speed * 4.0;
	vs_out.normal_coord2 = texcoord.xy * texture_scale * 4.0
	                     + normal_time * normal_speed * 8.0;

	gl_Position = vertex_world_to_clip * world_position;
}
