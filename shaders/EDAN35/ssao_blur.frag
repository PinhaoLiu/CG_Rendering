#version 410

uniform sampler2D ao_texture;
uniform sampler2D depth_texture;
uniform sampler2D normal_texture;

uniform mat4 projection_inverse;
uniform vec2 inverse_screen_resolution;
uniform float depth_sigma;

layout (location = 0) out float blurred_ambient_occlusion;


float reconstructViewDepth(vec2 texcoord, float depth)
{
	vec4 clip_position = vec4(texcoord * 2.0 - 1.0,
	                          depth * 2.0 - 1.0,
	                          1.0);
	vec4 view_position = projection_inverse * clip_position;
	return view_position.z / view_position.w;
}


void main()
{
	vec2 texcoord = gl_FragCoord.xy * inverse_screen_resolution;
	float centre_depth = texture(depth_texture, texcoord).r;
	if (centre_depth >= 1.0) {
		blurred_ambient_occlusion = 1.0;
		return;
	}

	float centre_view_depth = reconstructViewDepth(texcoord, centre_depth);
	vec3 centre_normal = normalize(texture(normal_texture, texcoord).xyz * 2.0 - 1.0);

	float weighted_ao = 0.0;
	float total_weight = 0.0;
	for (int y = -2; y <= 2; ++y) {
		for (int x = -2; x <= 2; ++x) {
			vec2 offset = vec2(float(x), float(y));
			vec2 sample_texcoord = texcoord + offset * inverse_screen_resolution;
			if (any(lessThan(sample_texcoord, vec2(0.0)))
			 || any(greaterThan(sample_texcoord, vec2(1.0))))
				continue;

			float sample_depth = texture(depth_texture, sample_texcoord).r;
			if (sample_depth >= 1.0)
				continue;

			float sample_view_depth = reconstructViewDepth(sample_texcoord, sample_depth);
			vec3 sample_normal = normalize(texture(normal_texture, sample_texcoord).xyz * 2.0 - 1.0);

			float spatial_weight = exp(-0.5 * dot(offset, offset) / 4.0);
			float depth_weight = exp(-abs(sample_view_depth - centre_view_depth)
			                         / max(depth_sigma, 0.0001));
			float normal_weight = pow(max(dot(centre_normal, sample_normal), 0.0), 16.0);
			float weight = spatial_weight * depth_weight * normal_weight;

			weighted_ao += texture(ao_texture, sample_texcoord).r * weight;
			total_weight += weight;
		}
	}

	blurred_ambient_occlusion = total_weight > 0.0
		? weighted_ao / total_weight
		: texture(ao_texture, texcoord).r;
}
