#version 410

uniform sampler2D depth_texture;
uniform sampler2D normal_texture;

uniform mat4 projection;
uniform mat4 projection_inverse;
uniform mat4 world_to_view;
uniform vec2 inverse_screen_resolution;
uniform float radius;
uniform float bias;
uniform float power;
uniform int sample_count;

layout (location = 0) out float ambient_occlusion;

const int MAX_SAMPLES = 32;
const float PI = 3.14159265359;


float randomFloat(float seed)
{
	return fract(sin(seed) * 43758.5453123);
}

vec3 reconstructViewPosition(vec2 texcoord, float depth)
{
	vec4 clip_position = vec4(texcoord * 2.0 - 1.0,
	                          depth * 2.0 - 1.0,
	                          1.0);
	vec4 view_position = projection_inverse * clip_position;
	return view_position.xyz / view_position.w;
}

vec3 kernelSample(int index, int count)
{
	float index_f = float(index);
	float u1 = randomFloat(17.0 + index_f * 23.0);
	float u2 = randomFloat(53.0 + index_f * 47.0);
	float phi = 2.0 * PI * u1;
	float radial = sqrt(u2);

	// Cosine-weighted local hemisphere whose positive axis is local +Z.
	vec3 direction = vec3(cos(phi) * radial,
	                      sin(phi) * radial,
	                      sqrt(max(1.0 - u2, 0.0)));
	float distribution = float(index + 1) / float(count);
	distribution = mix(0.1, 1.0, distribution * distribution);
	return direction * distribution;
}


void main()
{
	vec2 texcoord = gl_FragCoord.xy * inverse_screen_resolution;
	float centre_depth = texture(depth_texture, texcoord).r;
	if (centre_depth >= 1.0) {
		ambient_occlusion = 1.0;
		return;
	}

	vec3 position = reconstructViewPosition(texcoord, centre_depth);
	vec3 world_normal = normalize(texture(normal_texture, texcoord).xyz * 2.0 - 1.0);
	vec3 normal = normalize(mat3(world_to_view) * world_normal);

	// Build a stable per-pixel tangent direction and remove its normal
	// component.  GLSL matrix constructors take column vectors, so the matrix
	// below maps local +X/+Y/+Z to view-space tangent/bitangent/normal.
	float pixel_seed = dot(floor(gl_FragCoord.xy), vec2(12.9898, 78.233));
	vec3 random_vector = normalize(vec3(randomFloat(pixel_seed + 1.0) * 2.0 - 1.0,
	                                    randomFloat(pixel_seed + 2.0) * 2.0 - 1.0,
	                                    randomFloat(pixel_seed + 3.0) * 2.0 - 1.0));
	vec3 tangent = random_vector - normal * dot(random_vector, normal);
	if (dot(tangent, tangent) < 0.0001) {
		vec3 fallback_axis = abs(normal.z) < 0.999
			? vec3(0.0, 0.0, 1.0)
			: vec3(0.0, 1.0, 0.0);
		tangent = cross(fallback_axis, normal);
	}
	tangent = normalize(tangent);
	vec3 bitangent = normalize(cross(normal, tangent));
	mat3 tangent_to_view = mat3(tangent, bitangent, normal);

	float occlusion = 0.0;
	int samples = clamp(sample_count, 1, MAX_SAMPLES);
	for (int i = 0; i < MAX_SAMPLES; ++i) {
		if (i >= samples)
			break;

		vec3 sample_position = position
		                     + tangent_to_view * kernelSample(i, samples) * radius;
		vec4 sample_clip = projection * vec4(sample_position, 1.0);
		if (sample_clip.w <= 0.0)
			continue;

		vec2 sample_texcoord = sample_clip.xy / sample_clip.w * 0.5 + 0.5;
		if (any(lessThan(sample_texcoord, vec2(0.0)))
		 || any(greaterThan(sample_texcoord, vec2(1.0))))
			continue;

		float scene_depth = texture(depth_texture, sample_texcoord).r;
		if (scene_depth >= 1.0)
			continue;

		vec3 scene_position = reconstructViewPosition(sample_texcoord, scene_depth);
		// OpenGL view space looks down -Z.  A larger Z value is closer to the
		// camera, so it occludes a sample when it is in front by more than bias.
		float blocked = scene_position.z >= sample_position.z + bias ? 1.0 : 0.0;
		float depth_delta = abs(position.z - scene_position.z);
		float range_weight = smoothstep(0.0, 1.0, radius / max(depth_delta, 0.0001));
		occlusion += blocked * range_weight;
	}

	float visibility = 1.0 - occlusion / float(samples);
	ambient_occlusion = pow(clamp(visibility, 0.0, 1.0), max(power, 0.0001));
}
