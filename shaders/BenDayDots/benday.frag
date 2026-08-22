#version 410

uniform sampler2D scene_color;
uniform sampler2D scene_depth;
uniform sampler2D scene_normal;
uniform vec2 framebuffer_size;
uniform vec2 inverse_framebuffer_size;
uniform float camera_near;
uniform float camera_far;
uniform bool effect_enabled;
uniform int color_mode;
uniform float cell_size_pixels;
uniform float inverse_cell_size_pixels;
uniform vec3 channel_dot_scales;
uniform mat2 channel_rotations[3];
uniform vec2 registration_offsets[3];
uniform float exposure_scale;
uniform float tone_contrast;
uniform float inverse_tone_gamma;
uniform float tone_intensity;
uniform bool posterization_enabled;
uniform int posterization_levels;
uniform bool outlines_enabled;
uniform float outline_strength;
uniform float depth_outline_threshold;
uniform float normal_outline_threshold;
uniform float normal_outline_weight;
uniform float outline_thickness_pixels;
uniform bool surface_texture_enabled;
uniform float paper_grain_strength;
uniform float inverse_paper_grain_scale_pixels;
uniform float ink_variation_strength;

in vec2 uv;
out vec4 frag_color;

vec2 hash22(vec2 point)
{
	vec3 p = fract(vec3(point.xyx) * vec3(0.1031, 0.1030, 0.0973));
	p += dot(p, p.yzx + 33.33);
	return fract((p.xx + p.yz) * p.zy);
}

vec2 perlinGradient(vec2 lattice_point)
{
	vec2 gradient = hash22(lattice_point) * 2.0 - 1.0;
	return gradient * inversesqrt(max(dot(gradient, gradient), 0.0001));
}

float perlinNoise(vec2 point)
{
	vec2 cell = floor(point);
	vec2 local = fract(point);
	vec2 fade = local * local * local
	          * (local * (local * 6.0 - 15.0) + 10.0);

	float bottom_left = dot(perlinGradient(cell), local);
	float bottom_right = dot(perlinGradient(cell + vec2(1.0, 0.0)),
	                         local - vec2(1.0, 0.0));
	float top_left = dot(perlinGradient(cell + vec2(0.0, 1.0)),
	                      local - vec2(0.0, 1.0));
	float top_right = dot(perlinGradient(cell + vec2(1.0, 1.0)),
	                       local - vec2(1.0, 1.0));
	float bottom = mix(bottom_left, bottom_right, fade.x);
	float top = mix(top_left, top_right, fade.x);
	return clamp(mix(bottom, top, fade.y) * 1.41421356237, -1.0, 1.0);
}

vec3 adjustTone(vec3 color)
{
	color = clamp(color * exposure_scale, 0.0, 1.0);
	color = clamp((color - vec3(0.5)) * tone_contrast + vec3(0.5),
	              0.0, 1.0);
	color = pow(color, vec3(inverse_tone_gamma));
	if (posterization_enabled) {
		float intervals = float(max(posterization_levels, 2) - 1);
		color = floor(color * intervals + vec3(0.5)) / intervals;
	}
	return color;
}

float viewDepth(vec2 coordinate)
{
	float depth = texture(scene_depth, coordinate).r;
	float clip_depth = depth * 2.0 - 1.0;
	float denominator = camera_far + camera_near
	                  - clip_depth * (camera_far - camera_near);
	return (2.0 * camera_near * camera_far) / max(denominator, 0.0001);
}

vec3 worldNormal(vec2 coordinate)
{
	vec3 encoded_normal = texture(scene_normal, coordinate).xyz;
	return normalize(encoded_normal * 2.0 - 1.0);
}

float depthSobelEdge(vec2 coordinate)
{
	vec2 texel = outline_thickness_pixels * inverse_framebuffer_size;
	float top_left = viewDepth(coordinate + texel * vec2(-1.0, 1.0));
	float top = viewDepth(coordinate + texel * vec2(0.0, 1.0));
	float top_right = viewDepth(coordinate + texel * vec2(1.0, 1.0));
	float left = viewDepth(coordinate + texel * vec2(-1.0, 0.0));
	float center = viewDepth(coordinate);
	float right = viewDepth(coordinate + texel * vec2(1.0, 0.0));
	float bottom_left = viewDepth(coordinate + texel * vec2(-1.0, -1.0));
	float bottom = viewDepth(coordinate + texel * vec2(0.0, -1.0));
	float bottom_right = viewDepth(coordinate + texel * vec2(1.0, -1.0));

	float gradient_x = top_right + 2.0 * right + bottom_right
	                 - top_left - 2.0 * left - bottom_left;
	float gradient_y = bottom_left + 2.0 * bottom + bottom_right
	                 - top_left - 2.0 * top - top_right;
	float relative_gradient = length(vec2(gradient_x, gradient_y))
	                        / max(center, camera_near);
	float upper_threshold = max(depth_outline_threshold * 2.0,
	                            depth_outline_threshold + 0.0001);
	return smoothstep(depth_outline_threshold, upper_threshold,
	                  relative_gradient);
}

float normalSobelEdge(vec2 coordinate)
{
	vec2 texel = outline_thickness_pixels * inverse_framebuffer_size;
	vec3 top_left = worldNormal(coordinate + texel * vec2(-1.0, 1.0));
	vec3 top = worldNormal(coordinate + texel * vec2(0.0, 1.0));
	vec3 top_right = worldNormal(coordinate + texel * vec2(1.0, 1.0));
	vec3 left = worldNormal(coordinate + texel * vec2(-1.0, 0.0));
	vec3 right = worldNormal(coordinate + texel * vec2(1.0, 0.0));
	vec3 bottom_left = worldNormal(coordinate + texel * vec2(-1.0, -1.0));
	vec3 bottom = worldNormal(coordinate + texel * vec2(0.0, -1.0));
	vec3 bottom_right = worldNormal(coordinate + texel * vec2(1.0, -1.0));

	vec3 gradient_x = top_right + 2.0 * right + bottom_right
	                - top_left - 2.0 * left - bottom_left;
	vec3 gradient_y = bottom_left + 2.0 * bottom + bottom_right
	                - top_left - 2.0 * top - top_right;
	float gradient = sqrt(dot(gradient_x, gradient_x)
	                    + dot(gradient_y, gradient_y));
	float upper_threshold = max(normal_outline_threshold * 2.0,
	                            normal_outline_threshold + 0.0001);
	return smoothstep(normal_outline_threshold, upper_threshold, gradient);
}

float combinedOutline(vec2 coordinate)
{
	float depth_edge = depthSobelEdge(coordinate);
	float normal_edge = 0.0;
	if (normal_outline_weight > 0.0001)
		normal_edge = normalSobelEdge(coordinate) * normal_outline_weight;
	return max(depth_edge, normal_edge) * outline_strength;
}

float channelIntensity(vec3 color, int channel)
{
	float value;
	if (color_mode == 0) {
		float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
		value = 1.0 - luminance;
	} else if (color_mode == 2) {
		value = 1.0 - color[channel];
	} else {
		value = color[channel];
	}
	return clamp(value * tone_intensity, 0.0, 1.0);
}

float channelDot(vec2 pixel_from_center, int channel)
{
	vec2 registration_offset = registration_offsets[channel];
	vec2 registered_pixel = pixel_from_center - registration_offset;
	mat2 rotation = channel_rotations[channel];
	vec2 rotated_pixel = rotation * registered_pixel;
	vec2 cell_center = (floor(rotated_pixel * inverse_cell_size_pixels)
	                  + vec2(0.5)) * cell_size_pixels;
	vec2 sample_pixel = transpose(rotation) * cell_center + registration_offset
	                  + 0.5 * framebuffer_size;
	vec2 half_texel = 0.5 * inverse_framebuffer_size;
	vec2 sample_uv = clamp(sample_pixel * inverse_framebuffer_size,
	                       half_texel, vec2(1.0) - half_texel);
	vec3 sampled_color = adjustTone(texture(scene_color, sample_uv).rgb);
	float channel_intensity = clamp(channelIntensity(sampled_color, channel),
	                                0.0, 1.0);
	if (channel_intensity <= 0.0001)
		return 0.0;

	float radius = 0.5 * cell_size_pixels * channel_dot_scales[channel]
	             * sqrt(channel_intensity);
	if (surface_texture_enabled && ink_variation_strength > 0.0001) {
		vec2 noise_offset = vec2(float(channel) * 19.37,
		                         float(channel) * 47.11);
		const float ink_frequency_per_cell = 0.35;
		float ink_noise = perlinNoise((registered_pixel + noise_offset)
		                              * inverse_cell_size_pixels
		                              * ink_frequency_per_cell);
		radius *= max(0.0, 1.0 + ink_noise * ink_variation_strength);
	}
	float distance_to_center = length(rotated_pixel - cell_center);
	float antialias_width = max(fwidth(distance_to_center), 0.75);
	return 1.0 - smoothstep(radius - antialias_width,
	                        radius + antialias_width,
	                        distance_to_center);
}

vec3 applyFinish(vec3 color)
{
	if (surface_texture_enabled && paper_grain_strength > 0.0001) {
		float grain = perlinNoise(gl_FragCoord.xy
		                        * inverse_paper_grain_scale_pixels);
		if (color_mode == 1) {
			color *= 1.0 + grain * paper_grain_strength * 0.5;
		} else {
			vec3 paper = vec3(1.0, 0.985, 0.95)
			           * (1.0 + grain * paper_grain_strength);
			color *= clamp(paper, 0.0, 1.0);
		}
	}

	if (outlines_enabled && outline_strength > 0.0001) {
		float outline = clamp(combinedOutline(uv), 0.0, 1.0);
		vec3 outline_color = color_mode == 1 ? vec3(1.0) : vec3(0.0);
		color = mix(color, outline_color, outline);
	}
	return clamp(color, 0.0, 1.0);
}

void main()
{
	vec3 scene = texture(scene_color, uv).rgb;
	if (!effect_enabled) {
		frag_color = vec4(scene, 1.0);
		return;
	}

	vec2 pixel_from_center = gl_FragCoord.xy - 0.5 * framebuffer_size;
	vec3 result;
	if (color_mode == 0) {
		float ink = channelDot(pixel_from_center, 0);
		result = vec3(1.0 - ink);
	} else {
		vec3 dots;
		dots.r = channelDot(pixel_from_center, 0);
		dots.g = channelDot(pixel_from_center, 1);
		dots.b = channelDot(pixel_from_center, 2);
		result = color_mode == 2 ? vec3(1.0) - dots : dots;
	}
	frag_color = vec4(applyFinish(result), 1.0);
}
