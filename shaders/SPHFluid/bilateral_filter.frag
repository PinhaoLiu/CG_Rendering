#version 430

in vec2 uv;

layout (location = 0) out float filtered_value;

layout (binding = 0) uniform sampler2D uSource;
layout (binding = 1) uniform sampler2D uGuideDepth;
uniform ivec2 uDirection;
uniform vec2 uViewportSize;
uniform float uWorldRadiusMetres;
uniform float uSpatialSigmaFactor;
uniform float uDepthFalloffPerMetre;
uniform float uProjectionScaleY;
uniform float uDepthSentinel;
uniform int uMaximumRadiusPixels;
uniform bool uDepthField;

bool valid_depth(float depth)
{
	return depth > 0.0 && depth < 0.5 * uDepthSentinel
		&& !isnan(depth) && !isinf(depth);
}

void main()
{
	ivec2 size = ivec2(uViewportSize);
	ivec2 center_pixel = ivec2(gl_FragCoord.xy);
	float center_depth = texelFetch(uGuideDepth, center_pixel, 0).r;
	if (!valid_depth(center_depth)) {
		filtered_value = uDepthField ? uDepthSentinel : 0.0;
		return;
	}

	float projected_radius = uWorldRadiusMetres * uProjectionScaleY
		* 0.5 * uViewportSize.y / max(center_depth, 1.0e-5);
	int radius = clamp(int(ceil(projected_radius)), 1, uMaximumRadiusPixels);
	float sigma = max(0.5, float(radius) * uSpatialSigmaFactor);
	float inverse_two_sigma_squared = 0.5 / (sigma * sigma);

	float weighted_sum = 0.0;
	float weight_sum = 0.0;
	for (int offset = -32; offset <= 32; ++offset) {
		if (abs(offset) > radius)
			continue;
		ivec2 sample_pixel = center_pixel + offset * uDirection;
		if (any(lessThan(sample_pixel, ivec2(0))) ||
		    any(greaterThanEqual(sample_pixel, size)))
			continue;

		float sample_depth = texelFetch(uGuideDepth, sample_pixel, 0).r;
		if (!valid_depth(sample_depth))
			continue;
		float sample_value = texelFetch(uSource, sample_pixel, 0).r;
		if (isnan(sample_value) || isinf(sample_value) ||
		    (uDepthField && !valid_depth(sample_value)) ||
		    (!uDepthField && sample_value < 0.0))
			continue;

		float spatial_weight = exp(-float(offset * offset) * inverse_two_sigma_squared);
		float range_weight = exp(-abs(sample_depth - center_depth)
			* uDepthFalloffPerMetre);
		float weight = spatial_weight * range_weight;
		weighted_sum += weight * sample_value;
		weight_sum += weight;
	}

	if (weight_sum <= 1.0e-8)
		filtered_value = uDepthField ? uDepthSentinel : 0.0;
	else
		filtered_value = weighted_sum / weight_sum;
}
